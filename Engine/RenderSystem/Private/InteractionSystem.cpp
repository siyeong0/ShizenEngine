#include "pch.h"
#include "Engine/RenderSystem/Public/InteractionSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"

#include "Engine/RenderSystem/Public/TerrainSystem.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	static inline uint32 DivUp(uint32 x, uint32 d) { return (x + d - 1u) / d; }

	static inline uint32 WrapU32(int64 v, uint32 m)
	{
		int64 r = v % (int64)m;
		if (r < 0) r += (int64)m;
		return (uint32)r;
	}

	void InteractionSystem::InstallPasses(Renderer& renderer, TerrainSystem& terrain)
	{
		// Interaction field texture (R16_FLOAT SRV/UAV)
		{
			TextureDesc td = {};
			td.Name = "InteractionField";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = INTERACTION_FIELD_RESOLUTION;
			td.Height = INTERACTION_FIELD_RESOLUTION;
			td.Format = TEX_FORMAT_R16_FLOAT;
			td.MipLevels = 1;
			td.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
			td.Usage = USAGE_DEFAULT;

			renderer.AddTexture(STRING_HASH("InteractionField"), td);
		}

		// Interaction stamps (Structured, dynamic CPU write)
		{
			BufferDesc bd = {};
			bd.Name = "InteractionStampBuffer";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_SHADER_RESOURCE;
			bd.Mode = BUFFER_MODE_STRUCTURED;
			bd.ElementByteStride = sizeof(hlsl::InteractionStamp);
			bd.Size = uint64(MAX_NUM_INTERACTION_STAMPS) * uint64(sizeof(hlsl::InteractionStamp));
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;

			renderer.AddBuffer(STRING_HASH("InteractionStampBuffer"), bd);
		}

		// Interaction global constants (per frame)
		{
			BufferDesc bd = {};
			bd.Name = "InteractionConstantsCB";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			bd.Size = uint64(sizeof(hlsl::InteractionConstants));

			renderer.AddBuffer(STRING_HASH("InteractionConstantsCB"), bd);
		}

		// Interaction dispatch constants (per rect dispatch)
		{
			BufferDesc bd = {};
			bd.Name = "InteractionDispatchCB";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			bd.Size = uint64(sizeof(hlsl::InteractionDispatch));

			renderer.AddBuffer(STRING_HASH("InteractionDispatchCB"), bd);
		}

		renderer.AddPass(
			"GrassInteraction",
			[&](RenderPassBuilder& b)
			{
				// UAV (read/write)
				b.DeclareTextureUAV(STRING_HASH("InteractionField"), RENDER_ACCESS_READWRITE);

				// SRV inputs
				b.DeclareBufferSRVRead(STRING_HASH("InteractionStampBuffer"));

				// CBVs
				b.DeclareBufferCBVRead(STRING_HASH("InteractionConstantsCB"));
				b.DeclareBufferCBVRead(STRING_HASH("InteractionDispatchCB"));
			},
			[this, &terrain](RenderPassContext& ctx)
			{
				(void)terrain;

				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pScene, "Scene is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");

				ASSERT(m_pDecayCSO && m_pDecaySRB, "Decay PSO/SRB not ready.");
				ASSERT(m_pClearRectCSO && m_pClearRectSRB, "Clear PSO/SRB not ready.");
				ASSERT(m_pApplyStampCSO && m_pApplyStampSRB, "Apply PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// ------------------------------------------------------------
				// (1) Upload stamps (WORLD space)
				// ------------------------------------------------------------
				uint32 stampCount = 0;
				std::vector<hlsl::InteractionStamp> stampsWS;
				{
					ctx.pScene->ConsumeInteractionStamps(&stampsWS);
					stampCount = (uint32)std::min<size_t>(stampsWS.size(), MAX_NUM_INTERACTION_STAMPS);

					MapHelper<hlsl::InteractionStamp> stampMap(
						pContext,
						ctx.pRegistry->GetBuffer(STRING_HASH("InteractionStampBuffer")),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					for (uint32 i = 0; i < stampCount; ++i)
					{
						stampMap[i] = stampsWS[i];
					}
				}

				// ------------------------------------------------------------
				// (2) Compute sliding origin + update ring texel origin
				// ------------------------------------------------------------
				const float fieldWorldSize = INTERACTION_FIELD_WORLD_SIZE;
				const float2 worldSizeXZ = float2(fieldWorldSize, fieldWorldSize);

				const ViewFamily& viewFamily = *ctx.pViewFamily;
				ASSERT(!viewFamily.Views.empty(), "ViewFamily has no views.");
				const View& view = viewFamily.Views[0];

				const float2 camXZ = float2(view.CameraPosition.x, view.CameraPosition.z);
				float2 desiredOrigin = camXZ - 0.5f * worldSizeXZ;

				const float2 texelWorld = worldSizeXZ / float2((float)INTERACTION_FIELD_RESOLUTION, (float)INTERACTION_FIELD_RESOLUTION);

				desiredOrigin.x = std::floor(desiredOrigin.x / texelWorld.x) * texelWorld.x;
				desiredOrigin.y = std::floor(desiredOrigin.y / texelWorld.y) * texelWorld.y;

				bool bClearAll = false;
				std::vector<uint4> clearRects;

				auto addRect = [&](uint32 x, uint32 y, uint32 w, uint32 h)
				{
					if (w == 0 || h == 0)
					{
						return;
					}
					uint4 r; r.x = x; r.y = y; r.z = w; r.w = h;
					clearRects.push_back(r);
				};

				if (!m_bHasPrevOrigin)
				{
					m_bHasPrevOrigin = true;
					m_PrevFieldOriginXZ = desiredOrigin;
					m_TexelOriginX = 0;
					m_TexelOriginY = 0;

					bClearAll = true; // first frame clear
				}
				else
				{
					const float2 deltaOrigin = desiredOrigin - m_PrevFieldOriginXZ;

					const int64 shiftX = (int64)std::llround(deltaOrigin.x / texelWorld.x);
					const int64 shiftY = (int64)std::llround(deltaOrigin.y / texelWorld.y);

					if (shiftX != 0 || shiftY != 0)
					{
						m_TexelOriginX = WrapU32((int64)m_TexelOriginX + shiftX, INTERACTION_FIELD_RESOLUTION);
						m_TexelOriginY = WrapU32((int64)m_TexelOriginY + shiftY, INTERACTION_FIELD_RESOLUTION);

						const uint32 W = INTERACTION_FIELD_RESOLUTION;
						const uint32 H = INTERACTION_FIELD_RESOLUTION;

						if (shiftX != 0)
						{
							const uint32 w = (uint32)std::min<int64>(std::llabs(shiftX), (int64)W);
							if (shiftX > 0) addRect(W - w, 0, w, H);
							else            addRect(0, 0, w, H);
						}

						if (shiftY != 0)
						{
							const uint32 h = (uint32)std::min<int64>(std::llabs(shiftY), (int64)H);
							if (shiftY > 0) addRect(0, H - h, W, h);
							else            addRect(0, 0, W, h);
						}

						m_PrevFieldOriginXZ = desiredOrigin;
					}
				}

				// ------------------------------------------------------------
				// (3) Upload InteractionConstantsCB  (MUST be before any Dispatch)
				// ------------------------------------------------------------
				{
					MapHelper<hlsl::InteractionConstants> map(
						pContext,
						ctx.pRegistry->GetBuffer(STRING_HASH("InteractionConstantsCB")),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					map->FieldWidth = INTERACTION_FIELD_RESOLUTION;
					map->FieldHeight = INTERACTION_FIELD_RESOLUTION;
					map->NumStamps = stampCount;
					map->DeltaTime = ctx.DeltaTime;

					map->DecayPerSec = 0.35f;
					map->ClampMax = 1.0f;
					map->ClampMin = 0.0f;
					map->_Pad0 = 0.0f;

					map->FieldOriginXZ = desiredOrigin;
					map->FieldWorldSizeXZ = worldSizeXZ;

					map->TexelOrigin = { m_TexelOriginX, m_TexelOriginY };
					map->_Pad1 = 0;
					map->_Pad2 = 0;
				}

				// Common UAV bind (allow overwrite)
				auto bindInteractionUAV = [&](IShaderResourceBinding* pSRB)
				{
					if (auto* var = pSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")),
							SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
				};

				// ------------------------------------------------------------
				// (4) Clear rects (first frame: full clear, else: exposed strips)
				// ------------------------------------------------------------
				if (bClearAll || !clearRects.empty())
				{
					bindInteractionUAV(m_pClearRectSRB);
					pContext->SetPipelineState(m_pClearRectCSO);
					pContext->CommitShaderResources(m_pClearRectSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					auto DispatchClearRect = [&](uint32 x, uint32 y, uint32 w, uint32 h)
					{
						// per-dispatch CB
						{
							MapHelper<hlsl::InteractionDispatch> dispMap(
								pContext,
								ctx.pRegistry->GetBuffer(STRING_HASH("InteractionDispatchCB")),
								MAP_WRITE,
								MAP_FLAG_DISCARD);

							dispMap->RectOffset = { x, y };
							dispMap->RectSize = { w, h };
							dispMap->StampIndex = 0;
							dispMap->_Pad = {};
						}

						DispatchComputeAttribs disp = {};
						disp.ThreadGroupCountX = DivUp(w, THREAD_GROUP_SIZE_X);
						disp.ThreadGroupCountY = DivUp(h, THREAD_GROUP_SIZE_Y);
						disp.ThreadGroupCountZ = 1;

						pContext->DispatchCompute(disp);
					};

					if (bClearAll)
					{
						DispatchClearRect(0, 0, INTERACTION_FIELD_RESOLUTION, INTERACTION_FIELD_RESOLUTION);
					}
					else
					{
						for (const uint4& r : clearRects)
						{
							DispatchClearRect(r.x, r.y, r.z, r.w);
						}
					}
				}

				// ------------------------------------------------------------
				// (5) Decay (full field)
				// ------------------------------------------------------------
				{
					bindInteractionUAV(m_pDecaySRB);

					pContext->SetPipelineState(m_pDecayCSO);
					pContext->CommitShaderResources(m_pDecaySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					DispatchComputeAttribs disp = {};
					disp.ThreadGroupCountX = DivUp(INTERACTION_FIELD_RESOLUTION, THREAD_GROUP_SIZE_X);
					disp.ThreadGroupCountY = DivUp(INTERACTION_FIELD_RESOLUTION, THREAD_GROUP_SIZE_Y);
					disp.ThreadGroupCountZ = 1;

					pContext->DispatchCompute(disp);
				}

				// ------------------------------------------------------------
				// (6) Apply stamps (per-stamp rect dispatch)
				// ------------------------------------------------------------
				if (stampCount > 0)
				{
					bindInteractionUAV(m_pApplyStampSRB);

					pContext->SetPipelineState(m_pApplyStampCSO);
					pContext->CommitShaderResources(m_pApplyStampSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					const float2 invWorldSize = float2(1.0f / worldSizeXZ.x, 1.0f / worldSizeXZ.y);
					const float2 sizeF = float2((float)INTERACTION_FIELD_RESOLUTION, (float)INTERACTION_FIELD_RESOLUTION);

					for (uint32 si = 0; si < stampCount; ++si)
					{
						const hlsl::InteractionStamp& s = stampsWS[si];

						// WORLD bounds
						const float2 minW = s.CenterXZ - s.Radius;
						const float2 maxW = s.CenterXZ + s.Radius;

						// local uv in sliding window [0..1]
						float2 minUV = (minW - desiredOrigin) * invWorldSize;
						float2 maxUV = (maxW - desiredOrigin) * invWorldSize;

						if (maxUV.x < 0.0f || maxUV.y < 0.0f || minUV.x > 1.0f || minUV.y > 1.0f)
							continue;

						minUV = Vector2::Clamp(minUV, float2(0, 0), float2(1, 1));
						maxUV = Vector2::Clamp(maxUV, float2(0, 0), float2(1, 1));

						uint32 x0 = (uint32)std::floor(minUV.x * sizeF.x);
						uint32 y0 = (uint32)std::floor(minUV.y * sizeF.y);
						uint32 x1 = (uint32)std::ceil(maxUV.x * sizeF.x);
						uint32 y1 = (uint32)std::ceil(maxUV.y * sizeF.y);

						x1 = std::min<uint32>(x1, INTERACTION_FIELD_RESOLUTION);
						y1 = std::min<uint32>(y1, INTERACTION_FIELD_RESOLUTION);

						if (x1 <= x0 || y1 <= y0)
							continue;

						const uint32 rw = (x1 - x0);
						const uint32 rh = (y1 - y0);

						// per-dispatch CB
						{
							MapHelper<hlsl::InteractionDispatch> dispMap(
								pContext,
								ctx.pRegistry->GetBuffer(STRING_HASH("InteractionDispatchCB")),
								MAP_WRITE,
								MAP_FLAG_DISCARD);

							dispMap->RectOffset = { x0, y0 };
							dispMap->RectSize = { rw, rh };
							dispMap->StampIndex = si;
							dispMap->_Pad = {};
						}

						DispatchComputeAttribs disp = {};
						disp.ThreadGroupCountX = DivUp(rw, THREAD_GROUP_SIZE_X);
						disp.ThreadGroupCountY = DivUp(rh, THREAD_GROUP_SIZE_Y);
						disp.ThreadGroupCountZ = 1;

						pContext->DispatchCompute(disp);
					}
				}
			},
				[this, &renderer]()
			{
				// ============================================================
				// (1) Decay PSO
				// ============================================================
				{
					ShaderCreateInfo csCI = {};
					csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					csCI.EntryPoint = m_DecayEntry.c_str();
					csCI.Desc.Name = "InteractionDecayCS";
					csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
					csCI.Desc.UseCombinedTextureSamplers = false;
					csCI.FilePath = m_InteractionCS.c_str();

					RefCntAutoPtr<IShader> cs;
					renderer.CreateShader(csCI, &cs);
					ASSERT(cs, "InteractionDecayCS compile failed.");

					ComputePipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "PSO_InteractionDecay";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

					auto& rl = psoCI.PSODesc.ResourceLayout;
					rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					rl.Variables = vars;
					rl.NumVariables = _countof(vars);

					psoCI.pCS = cs;

					m_pDecayCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pDecayCSO, "AcquireCompute(InteractionDecay) failed.");

					m_pDecayCSO->CreateShaderResourceBinding(&m_pDecaySRB, true);
					ASSERT(m_pDecaySRB, "InteractionDecay SRB create failed.");

					if (auto* var = m_pDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
					{
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionConstantsCB")));
					}
				}

				// ============================================================
				// (2) ClearRect PSO
				// ============================================================
				{
					ShaderCreateInfo csCI = {};
					csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					csCI.EntryPoint = m_ClearEntry.c_str();
					csCI.Desc.Name = "InteractionClearRectCS";
					csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
					csCI.Desc.UseCombinedTextureSamplers = false;
					csCI.FilePath = m_InteractionCS.c_str();

					RefCntAutoPtr<IShader> cs;
					renderer.CreateShader(csCI, &cs);
					ASSERT(cs, "InteractionClearRectCS compile failed.");

					ComputePipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "PSO_InteractionClearRect";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

					auto& rl = psoCI.PSODesc.ResourceLayout;
					rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_DISPATCH",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					rl.Variables = vars;
					rl.NumVariables = _countof(vars);

					psoCI.pCS = cs;

					m_pClearRectCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pClearRectCSO, "AcquireCompute(InteractionClearRect) failed.");

					m_pClearRectCSO->CreateShaderResourceBinding(&m_pClearRectSRB, true);
					ASSERT(m_pClearRectSRB, "InteractionClearRect SRB create failed.");

					if (auto* var = m_pClearRectSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
					{
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionConstantsCB")));
					}

					if (auto* var = m_pClearRectSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_DISPATCH"))
					{
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionDispatchCB")));
					}
				}

				// ============================================================
				// (3) ApplyStamp PSO
				// ============================================================
				{
					ShaderCreateInfo csCI = {};
					csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					csCI.EntryPoint = m_ApplyEntry.c_str();
					csCI.Desc.Name = "InteractionApplyStampRectCS";
					csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
					csCI.Desc.UseCombinedTextureSamplers = false;
					csCI.FilePath = m_InteractionCS.c_str();

					RefCntAutoPtr<IShader> cs;
					renderer.CreateShader(csCI, &cs);
					ASSERT(cs, "InteractionApplyStampRectCS compile failed.");

					ComputePipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "PSO_InteractionApplyStampRect";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

					auto& rl = psoCI.PSODesc.ResourceLayout;
					rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "g_Stamps",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_DISPATCH",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					rl.Variables = vars;
					rl.NumVariables = _countof(vars);

					psoCI.pCS = cs;

					m_pApplyStampCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pApplyStampCSO, "AcquireCompute(InteractionApplyStampRect) failed.");

					m_pApplyStampCSO->CreateShaderResourceBinding(&m_pApplyStampSRB, true);
					ASSERT(m_pApplyStampSRB, "InteractionApplyStampRect SRB create failed.");

					if (auto* var = m_pApplyStampSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
					{
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionConstantsCB")));
					}

					if (auto* var = m_pApplyStampSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_DISPATCH"))
					{
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionDispatchCB")));
					}

					if (auto* var = m_pApplyStampSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Stamps"))
					{
						var->Set(renderer.GetBufferSRV(STRING_HASH("InteractionStampBuffer")));
					}
				}
			});
	}

	float2 InteractionSystem::GetWorldOriginXZ() const
	{
		return m_PrevFieldOriginXZ;
	}

	float2 InteractionSystem::GetWorldSizeXZ() const
	{
		const float s = INTERACTION_FIELD_WORLD_SIZE;
		return float2(s, s);
	}

	uint2 InteractionSystem::GetTexelOrigin() const
	{
		return { m_TexelOriginX, m_TexelOriginY };
	}

} // namespace shz
