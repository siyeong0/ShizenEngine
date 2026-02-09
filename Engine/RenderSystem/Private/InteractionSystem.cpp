#include "pch.h"
#include "Engine/RenderSystem/Public/InteractionSystem.h"

#include <algorithm>
#include <cmath>

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

	struct ClearRect
	{
		// local rect in [0..W)
		uint32 X = 0, Y = 0, W = 0, H = 0;
	};

	// Split a local-space rect that might wrap in ring-space due to TexelOrigin.
	// We keep rect in LOCAL space; shader converts local->ring via TexelOrigin, so wrapping happens there automatically.
	// => We do NOT need to split for wrap! We only need local rect definition.
	// (Important) Local rect is always within [0..FieldSize) without wrap. We'll generate it like that.

	void InteractionSystem::InstallPasses(Renderer& renderer, TerrainSystem& terrain)
	{
		// Interaction field texture (R16_FLOAT SRV/UAV)
		{
			TextureDesc td = {};
			td.Name = "InteractionField";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = INTERACTION_FIELD_SIZE;
			td.Height = INTERACTION_FIELD_SIZE;
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

		// Interaction dispatch constants (per rect dispatch; small)
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
				b.DeclareTextureUAV(STRING_HASH("InteractionField"), RENDER_ACCESS_READWRITE);

				b.DeclareBufferSRVRead(STRING_HASH("InteractionStampBuffer"));
				b.DeclareBufferCBVRead(STRING_HASH("InteractionConstantsCB"));
				b.DeclareBufferCBVRead(STRING_HASH("InteractionDispatchCB"));

				// 더 이상 HeightFieldConstantsCB가 필요 없음 (InteractionField 자체가 로컬 월드 매핑)
			},
			[this, &terrain](RenderPassContext& ctx)
			{
				(void)terrain;

				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pScene, "Scene is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pDecayCSO && m_pDecaySRB, "Decay PSO/SRB not ready.");
				ASSERT(m_pRectOpCSO && m_pRectOpSRB, "RectOp PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// ------------------------------------------------------------
				// (A) Upload stamps (WORLD space)
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
						stampMap[i] = stampsWS[i];
				}

				// ------------------------------------------------------------
				// (B) Compute sliding origin (snapped to texels)
				// ------------------------------------------------------------
				const float fieldWorldSize = INTERACTION_FIELD_WORLD_SIZE;
				const float2 worldSizeXZ = float2(fieldWorldSize, fieldWorldSize);

				// Center field around camera/player
				const ViewFamily& viewFamily = *ctx.pViewFamily;
				ASSERT(!viewFamily.Views.empty(), "ViewFamily has no views.");
				const View& view = viewFamily.Views[0];

				const float2 camXZ = float2(view.CameraPosition.x, view.CameraPosition.z);
				float2 desiredOrigin = camXZ - 0.5f * worldSizeXZ;

				// snap origin to texel grid so shifts are integer texels
				const float2 texelWorld = worldSizeXZ / float2((float)INTERACTION_FIELD_SIZE, (float)INTERACTION_FIELD_SIZE);
				desiredOrigin.x = std::floor(desiredOrigin.x / texelWorld.x) * texelWorld.x;
				desiredOrigin.y = std::floor(desiredOrigin.y / texelWorld.y) * texelWorld.y;

				int64 shiftX = 0;
				int64 shiftY = 0;

				if (!m_bHasPrevOrigin)
				{
					m_bHasPrevOrigin = true;
					m_PrevFieldOriginXZ = desiredOrigin;
					m_TexelOriginX = 0;
					m_TexelOriginY = 0;

					// First frame: clear whole field once
					// We'll do it using RectOp clear with full rect.
					{
						MapHelper<hlsl::InteractionDispatch> dispMap(
							pContext,
							ctx.pRegistry->GetBuffer(STRING_HASH("InteractionDispatchCB")),
							MAP_WRITE,
							MAP_FLAG_DISCARD);

						dispMap->RectOffset = { 0, 0 };
						dispMap->RectSize = { INTERACTION_FIELD_SIZE, INTERACTION_FIELD_SIZE };
						dispMap->StampIndex = 0;
						dispMap->Mode = 0; // ClearRect
						dispMap->_Pad = { 0, 0 };
					}

					DispatchComputeAttribs disp = {};
					disp.ThreadGroupCountX = DivUp(INTERACTION_FIELD_SIZE, THREAD_GROUP_SIZE_X);
					disp.ThreadGroupCountY = DivUp(INTERACTION_FIELD_SIZE, THREAD_GROUP_SIZE_Y);
					disp.ThreadGroupCountZ = 1;

					if (auto* var = m_pRectOpSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")),
							SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					pContext->SetPipelineState(m_pRectOpCSO);
					pContext->CommitShaderResources(m_pRectOpSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					pContext->DispatchCompute(disp);
				}
				else
				{
					float2 deltaOrigin = desiredOrigin - m_PrevFieldOriginXZ;

					shiftX = (int64)std::llround(deltaOrigin.x / texelWorld.x);
					shiftY = (int64)std::llround(deltaOrigin.y / texelWorld.y);

					if (shiftX != 0 || shiftY != 0)
					{
						// update ring texel origin (mod field size)
						m_TexelOriginX = WrapU32((int64)m_TexelOriginX + shiftX, INTERACTION_FIELD_SIZE);
						m_TexelOriginY = WrapU32((int64)m_TexelOriginY + shiftY, INTERACTION_FIELD_SIZE);

						// clear newly exposed strips in LOCAL space
						// local space is [0..FieldSize) in the sliding window.
						// if shiftX > 0 => new area on the RIGHT side: local x in [FieldSize-shiftX .. FieldSize)
						// if shiftX < 0 => new area on the LEFT side : local x in [0 .. -shiftX)
						std::vector<ClearRect> clearRects;

						auto addRect = [&](uint32 x, uint32 y, uint32 w, uint32 h)
						{
							if (w == 0 || h == 0) return;
							ClearRect r; r.X = x; r.Y = y; r.W = w; r.H = h;
							clearRects.push_back(r);
						};

						const uint32 W = INTERACTION_FIELD_SIZE;
						const uint32 H = INTERACTION_FIELD_SIZE;

						if (shiftX != 0)
						{
							uint32 w = (uint32)std::min<int64>(std::llabs(shiftX), (int64)W);
							if (shiftX > 0) addRect(W - w, 0, w, H);
							else            addRect(0, 0, w, H);
						}

						if (shiftY != 0)
						{
							uint32 h = (uint32)std::min<int64>(std::llabs(shiftY), (int64)H);
							if (shiftY > 0) addRect(0, H - h, W, h);
							else            addRect(0, 0, W, h);
						}

						if (!clearRects.empty())
						{
							if (auto* var = m_pRectOpSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
							{
								var->Set(ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")),
									SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
							}

							pContext->SetPipelineState(m_pRectOpCSO);
							pContext->CommitShaderResources(m_pRectOpSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

							for (const ClearRect& r : clearRects)
							{
								MapHelper<hlsl::InteractionDispatch> dispMap(
									pContext,
									ctx.pRegistry->GetBuffer(STRING_HASH("InteractionDispatchCB")),
									MAP_WRITE,
									MAP_FLAG_DISCARD);

								dispMap->RectOffset = { r.X, r.Y };
								dispMap->RectSize = { r.W, r.H };
								dispMap->StampIndex = 0;
								dispMap->Mode = 0; // ClearRect
								dispMap->_Pad = { 0, 0 };

								DispatchComputeAttribs disp = {};
								disp.ThreadGroupCountX = DivUp(r.W, THREAD_GROUP_SIZE_X);
								disp.ThreadGroupCountY = DivUp(r.H, THREAD_GROUP_SIZE_Y);
								disp.ThreadGroupCountZ = 1;

								pContext->DispatchCompute(disp);
							}
						}

						m_PrevFieldOriginXZ = desiredOrigin;
					}
				}

				// ------------------------------------------------------------
				// (C) Upload global constants (per frame)
				// ------------------------------------------------------------
				{
					MapHelper<hlsl::InteractionConstants> map(
						pContext,
						ctx.pRegistry->GetBuffer(STRING_HASH("InteractionConstantsCB")),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					map->FieldWidth = INTERACTION_FIELD_SIZE;
					map->FieldHeight = INTERACTION_FIELD_SIZE;
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

				// ------------------------------------------------------------
				// (D) Decay (full field)
				// ------------------------------------------------------------
				{
					DispatchComputeAttribs disp = {};
					disp.ThreadGroupCountX = DivUp(INTERACTION_FIELD_SIZE, THREAD_GROUP_SIZE_X);
					disp.ThreadGroupCountY = DivUp(INTERACTION_FIELD_SIZE, THREAD_GROUP_SIZE_Y);
					disp.ThreadGroupCountZ = 1;

					if (auto* var = m_pDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")),
							SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					pContext->SetPipelineState(m_pDecayCSO);
					pContext->CommitShaderResources(m_pDecaySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					pContext->DispatchCompute(disp);
				}

				// ------------------------------------------------------------
				// (E) Apply stamps (STAMP별 Rect Dispatch)
				// ------------------------------------------------------------
				if (stampCount > 0)
				{
					if (auto* var = m_pRectOpSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")),
							SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					pContext->SetPipelineState(m_pRectOpCSO);
					pContext->CommitShaderResources(m_pRectOpSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					const float2 invWorldSize = float2(1.0f / worldSizeXZ.x, 1.0f / worldSizeXZ.y);
					const float2 sizeF = float2((float)INTERACTION_FIELD_SIZE, (float)INTERACTION_FIELD_SIZE);

					for (uint32 si = 0; si < stampCount; ++si)
					{
						const hlsl::InteractionStamp& s = stampsWS[si];

						// Stamp bounds in WORLD (AABB in XZ)
						float2 minW = s.CenterXZ - s.Radius;
						float2 maxW = s.CenterXZ + s.Radius;

						// Convert to local uv [0..1] within sliding window
						float2 minUV = (minW - desiredOrigin) * invWorldSize;
						float2 maxUV = (maxW - desiredOrigin) * invWorldSize;

						// If completely outside local window, skip
						// (We allow small margin)
						if (maxUV.x < 0.0f || maxUV.y < 0.0f || minUV.x > 1.0f || minUV.y > 1.0f)
							continue;

						// Clamp to [0..1] and convert to LOCAL texel rect
						minUV = Vector2::Clamp(minUV, float2(0, 0), float2(1, 1));
						maxUV = Vector2::Clamp(maxUV, float2(0, 0), float2(1, 1));

						uint32 x0 = (uint32)std::floor(minUV.x * sizeF.x);
						uint32 y0 = (uint32)std::floor(minUV.y * sizeF.y);
						uint32 x1 = (uint32)std::ceil(maxUV.x * sizeF.x);
						uint32 y1 = (uint32)std::ceil(maxUV.y * sizeF.y);

						x1 = std::min<uint32>(x1, INTERACTION_FIELD_SIZE);
						y1 = std::min<uint32>(y1, INTERACTION_FIELD_SIZE);

						if (x1 <= x0 || y1 <= y0)
							continue;

						uint32 rw = (x1 - x0);
						uint32 rh = (y1 - y0);

						// Update per-dispatch CB
						{
							MapHelper<hlsl::InteractionDispatch> dispMap(
								pContext,
								ctx.pRegistry->GetBuffer(STRING_HASH("InteractionDispatchCB")),
								MAP_WRITE,
								MAP_FLAG_DISCARD);

							dispMap->RectOffset = { x0, y0 };
							dispMap->RectSize = { rw, rh };
							dispMap->StampIndex = si;
							dispMap->Mode = 1; // ApplySingleStamp
							dispMap->_Pad = { 0, 0 };
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
				// -----------------------------
				// (1) Decay PSO
				// -----------------------------
				{
					ShaderCreateInfo csCI = {};
					csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					csCI.EntryPoint = "DecayInteractionField";
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
						{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					rl.Variables = vars;
					rl.NumVariables = _countof(vars);

					psoCI.pCS = cs;

					m_pDecayCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pDecayCSO, "AcquireCompute(InteractionDecay) failed.");

					m_pDecayCSO->CreateShaderResourceBinding(&m_pDecaySRB, true);
					ASSERT(m_pDecaySRB, "InteractionDecay SRB create failed.");

					if (auto* var = m_pDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionConstantsCB")));
				}

				// -----------------------------
				// (2) RectOp PSO (ClearRect / ApplySingleStamp)
				// -----------------------------
				{
					ShaderCreateInfo csCI = {};
					csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					csCI.EntryPoint = "RectOp";
					csCI.Desc.Name = "InteractionRectOpCS";
					csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
					csCI.Desc.UseCombinedTextureSamplers = false;
					csCI.FilePath = m_InteractionCS.c_str();

					RefCntAutoPtr<IShader> cs;
					renderer.CreateShader(csCI, &cs);
					ASSERT(cs, "InteractionRectOpCS compile failed.");

					ComputePipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "PSO_InteractionRectOp";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

					auto& rl = psoCI.PSODesc.ResourceLayout;
					rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "g_Stamps",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_DISPATCH",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					rl.Variables = vars;
					rl.NumVariables = _countof(vars);

					psoCI.pCS = cs;

					m_pRectOpCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pRectOpCSO, "AcquireCompute(InteractionRectOp) failed.");

					m_pRectOpCSO->CreateShaderResourceBinding(&m_pRectOpSRB, true);
					ASSERT(m_pRectOpSRB, "InteractionRectOp SRB create failed.");

					if (auto* var = m_pRectOpSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionConstantsCB")));

					if (auto* var = m_pRectOpSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_DISPATCH"))
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionDispatchCB")));

					if (auto* var = m_pRectOpSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Stamps"))
						var->Set(renderer.GetBufferSRV(STRING_HASH("InteractionStampBuffer")));
				}
			});
	}

	float2 InteractionSystem::GetWorldOriginXZ() const
	{
		return m_PrevFieldOriginXZ;
	}

	float2 InteractionSystem::GetWorldSizeXZ() const
	{
		// Fixed coverage size of the interaction field in WORLD space (meters).
		const float s = INTERACTION_FIELD_WORLD_SIZE;
		return float2(s, s);
	}

	uint2 InteractionSystem::GetTexelOrigin() const
	{
		return { m_TexelOriginX, m_TexelOriginY };
	}
} // namespace shz
