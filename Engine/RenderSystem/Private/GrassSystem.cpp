#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

#include "Engine/AssetManager/Public/AssetManager.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"

#include "Engine/RenderSystem/Public/IndirectArgsSystem.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	static inline float2 WorldXZToTerrainUV(
		int heightFieldWidth, int heightFieldHeight,
		float spacingX, float spacingY,
		uint centerXZ,
		const float2& worldXZ)
	{
		const float sizeX = float(std::max<int>(int(heightFieldWidth) - 1, 0)) * spacingX;
		const float sizeZ = float(std::max<int>(int(heightFieldHeight) - 1, 0)) * spacingY;

		const float originX = (centerXZ != 0) ? (-0.5f * sizeX) : 0.0f;
		const float originZ = (centerXZ != 0) ? (-0.5f * sizeZ) : 0.0f;

		const float invSizeX = 1.0f / std::max(sizeX, 1e-6f);
		const float invSizeZ = 1.0f / std::max(sizeZ, 1e-6f);

		return float2
		{
			(worldXZ.x - originX) * invSizeX,
			(worldXZ.y - originZ) * invSizeZ
		};
	}

	static inline float WorldRadiusToUv_MinAxis(
		int heightFieldWidth, int heightFieldHeight,
		float spacingX, float spacingY,
		float radiusWorld)
	{
		const float sizeX = float(std::max<int>(int(heightFieldWidth) - 1, 0)) * spacingX;
		const float sizeZ = float(std::max<int>(int(heightFieldHeight) - 1, 0)) * spacingY;
		const float sizeMin = std::max(std::min(sizeX, sizeZ), 1e-6f);
		return radiusWorld / sizeMin;
	}

	static inline uint32 DivUp(uint32 x, uint32 d) { return (x + d - 1u) / d; }

	// ---------------------------------------------------------------------
	// InstallPasses
	// ---------------------------------------------------------------------
	void GrassSystem::InstallPasses(Renderer& renderer, IndirectArgsSystem& indirect)
	{
		// Allocate indirect slot for grass
		m_IndirectSlot = indirect.AllocateSlot("GrassSystem");

		// Register template for grass slot
		{
			hlsl::IndirectArgsTemplate t = {};
			t.IndexCountPerInstance = 39; // TODO: mesh에서 얻는 값으로 교체
			t.StartIndexLocation = 0;
			t.BaseVertexLocation = 0;
			t.StartInstanceLocation = 0;

			indirect.SetTemplate(m_IndirectSlot, t);
		}

		// Resource IDs (centralized)
		const uint64 kInteractionField = STRING_HASH("InteractionField");
		const uint64 kInteractionStamps = STRING_HASH("InteractionStampBuffer");
		const uint64 kInteractionConstantsCB = STRING_HASH("InteractionConstantsCB");

		const uint64 kGrassGenCB = STRING_HASH("GrassGenConstantsCB");
		const uint64 kGrassRenderCB = STRING_HASH("GrassRenderConstantsCB");

		const uint64 kGrassDensityField = STRING_HASH("GrassDensityField");
		const uint64 kGrassInstanceBuffer = STRING_HASH("GrassInstanceBuffer");

		// Shared indirect resources (Renderer created)
		const uint64 kIndirectArgsBuffer = STRING_HASH("IndirectArgsBuffer");
		const uint64 kIndirectCountBuffer = STRING_HASH("IndirectCountBuffer");

		const uint64 kLightingTex = STRING_HASH("Lighting");
		const uint64 kDepthTex = STRING_HASH("GBufferDepth");
		const uint64 kShadowMap = STRING_HASH("ShadowMap");

		// =====================================================================
		// Pass 1) GrassInteraction (compute: decay + apply stamps)
		// =====================================================================
		renderer.AddPass(
			"GrassInteraction",
			[&](RenderPassBuilder& b)
			{
				// RW update target
				b.DeclareTextureUAV(kInteractionField, RENDER_ACCESS_READWRITE);

				// Inputs
				b.DeclareBufferSRVRead(kInteractionStamps);
				b.DeclareBufferCBVRead(kInteractionConstantsCB);

				// CPU also reads this for mapping / other passes may use
				b.DeclareBufferCBVRead(kGrassGenCB);
			},
			[this, kInteractionField, kInteractionStamps, kInteractionConstantsCB](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pScene, "Scene is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pInteractionDecayCSO && m_pInteractionDecaySRB, "InteractionDecay PSO/SRB not ready.");
				ASSERT(m_pInteractionApplyCSO && m_pInteractionApplySRB, "InteractionApply PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// (A) Upload stamps
				uint32 stampCount = 0;
				{
					MapHelper<hlsl::InteractionStamp> stampMap(
						pContext,
						ctx.pRegistry->GetBuffer(kInteractionStamps),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					std::vector<hlsl::InteractionStamp> interactionStamps;
					ctx.pScene->ConsumeInteractionStamps(&interactionStamps);

					stampCount = (uint32)std::min<size_t>(interactionStamps.size(), MAX_NUM_INTERACTION_STAMPS);

					for (uint32 i = 0; i < stampCount; ++i)
					{
						hlsl::InteractionStamp s = interactionStamps[i];

						// NOTE: old constants (1025/spacing(1,1)/center=1)
						s.CenterXZ = WorldXZToTerrainUV(1025, 1025, 1.0f, 1.0f, 1, s.CenterXZ);
						s.Radius = WorldRadiusToUv_MinAxis(1025, 1025, 1.0f, 1.0f, s.Radius);

						stampMap[i] = s;
					}
				}

				// (B) Upload constants
				{
					MapHelper<hlsl::InteractionConstants> map(
						pContext,
						ctx.pRegistry->GetBuffer(kInteractionConstantsCB),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					map->FieldWidth = INTERACTION_FIELD_SIZE;
					map->FieldHeight = INTERACTION_FIELD_SIZE;
					map->NumStamps = stampCount;
					map->DeltaTime = ctx.DeltaTime;

					map->DecayPerSec = 0.15f;
					map->ClampMax = 1.0f;
					map->ClampMin = 0.0f;
					map->_Pad0 = 0.0f;
				}

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = DivUp(INTERACTION_FIELD_SIZE, 8);
				disp.ThreadGroupCountY = DivUp(INTERACTION_FIELD_SIZE, 8);
				disp.ThreadGroupCountZ = 1;

				// (C) Decay
				{
					if (auto* var = m_pInteractionDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureUAV(kInteractionField), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					pContext->SetPipelineState(m_pInteractionDecayCSO);
					pContext->CommitShaderResources(m_pInteractionDecaySRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					pContext->DispatchCompute(disp);
				}

				// (D) Apply stamps
				if (stampCount > 0)
				{
					if (auto* var = m_pInteractionApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureUAV(kInteractionField), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					pContext->SetPipelineState(m_pInteractionApplyCSO);
					pContext->CommitShaderResources(m_pInteractionApplySRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					pContext->DispatchCompute(disp);
				}
			},
				[this, &renderer, kInteractionStamps, kInteractionConstantsCB]()
			{
				// ------------------------------------------------------------
				// (1) Decay PSO
				// ------------------------------------------------------------
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
						{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					rl.Variables = vars;
					rl.NumVariables = _countof(vars);

					psoCI.pCS = cs;

					m_pInteractionDecayCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pInteractionDecayCSO, "AcquireCompute(InteractionDecay) failed.");

					m_pInteractionDecayCSO->CreateShaderResourceBinding(&m_pInteractionDecaySRB, true);
					ASSERT(m_pInteractionDecaySRB, "InteractionDecay SRB create failed.");

					if (auto* var = m_pInteractionDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
					{
						var->Set(renderer.GetBuffer(kInteractionConstantsCB));
					}
				}

				// ------------------------------------------------------------
				// (2) ApplyStamps PSO
				// ------------------------------------------------------------
				{
					ShaderCreateInfo csCI = {};
					csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					csCI.EntryPoint = "ApplyInteractionStamps";
					csCI.Desc.Name = "InteractionApplyStampsCS";
					csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
					csCI.Desc.UseCombinedTextureSamplers = false;
					csCI.FilePath = m_InteractionCS.c_str();

					RefCntAutoPtr<IShader> cs;
					renderer.CreateShader(csCI, &cs);
					ASSERT(cs, "InteractionApplyStampsCS compile failed.");

					ComputePipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "PSO_InteractionApplyStamps";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

					auto& rl = psoCI.PSODesc.ResourceLayout;
					rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "g_Stamps",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					rl.Variables = vars;
					rl.NumVariables = _countof(vars);

					SamplerDesc linearClamp =
					{
						FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
						TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
					};
					ImmutableSamplerDesc samplers[] =
					{
						{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
					};
					rl.ImmutableSamplers = samplers;
					rl.NumImmutableSamplers = _countof(samplers);

					psoCI.pCS = cs;

					m_pInteractionApplyCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pInteractionApplyCSO, "AcquireCompute(InteractionApplyStamps) failed.");

					m_pInteractionApplyCSO->CreateShaderResourceBinding(&m_pInteractionApplySRB, true);
					ASSERT(m_pInteractionApplySRB, "InteractionApplyStamps SRB create failed.");

					if (auto* var = m_pInteractionApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
					{
						var->Set(renderer.GetBuffer(kInteractionConstantsCB));
					}
					if (auto* var = m_pInteractionApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Stamps"))
					{
						var->Set(renderer.GetBufferSRV(kInteractionStamps));
					}
				}
			});

		// =====================================================================
		// Pass 2) GrassGenerateInstances (compute)
		// =====================================================================
		renderer.AddPass(
			"GrassGenerateInstances",
			[&](RenderPassBuilder& b)
			{
				// Outputs
				b.DeclareBufferUAV(kGrassInstanceBuffer, RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(kIndirectCountBuffer, RENDER_ACCESS_WRITE); // <-- shared counts

				// Inputs
				b.DeclareTextureSRVRead(kGrassDensityField);
				b.DeclareTextureSRVRead(kInteractionField);

				// Constants
				b.DeclareBufferCBVRead(kGrassGenCB);
			},
			[this, kIndirectCountBuffer, kGrassDensityField, kInteractionField, kGrassInstanceBuffer, kGrassGenCB](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pScene, "Scene is null.");
				ASSERT(ctx.pScene->GetHeightMap(), "HeightMap is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGenCSO && m_pGenSRB, "GrassGenerate PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// (0) Reset counter for my slot (slot * 4 bytes)
				{
					const uint32 zero = 0;
					const uint32 offset = m_IndirectSlot * 4u;

					pContext->UpdateBuffer(
						ctx.pRegistry->GetBuffer(kIndirectCountBuffer),
						offset,
						sizeof(uint32),
						&zero,
						RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

					StateTransitionDesc trBack[] =
					{
						{
							ctx.pRegistry->GetBuffer(kIndirectCountBuffer),
							RESOURCE_STATE_UNKNOWN,
							RESOURCE_STATE_UNORDERED_ACCESS,
							STATE_TRANSITION_FLAG_UPDATE_STATE
						},
					};
					pContext->TransitionResourceStates(_countof(trBack), trBack);
				}

				// (1) Bind per-frame textures
				{
					if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightMap"))
					{
						var->Set(ctx.pScene->GetHeightMap()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
					if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
					{
						var->Set(ctx.pRegistry->GetTextureSRV(kGrassDensityField), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
					if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureSRV(kInteractionField), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					StateTransitionDesc tr =
					{
						ctx.pScene->GetHeightMap(),
						RESOURCE_STATE_UNKNOWN,
						RESOURCE_STATE_SHADER_RESOURCE,
						STATE_TRANSITION_FLAG_UPDATE_STATE
					};
					pContext->TransitionResourceStates(1, &tr);
				}

				// (2) Dispatch
				{
					pContext->SetPipelineState(m_pGenCSO);
					pContext->CommitShaderResources(m_pGenSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

					DispatchComputeAttribs disp = {};
					disp.ThreadGroupCountX = (2u * 32u + 8u - 1u) / 8u;
					disp.ThreadGroupCountY = (2u * 32u + 8u - 1u) / 8u;
					disp.ThreadGroupCountZ = 1;

					pContext->DispatchCompute(disp);
				}
			},
				[this, &renderer, kGrassInstanceBuffer, kIndirectCountBuffer, kGrassGenCB]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "GenerateGrassInstances";
				csCI.Desc.Name = "GrassGenerateInstancesCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "GrassGenerateInstancesCS compile failed.");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_GrassGenerateInstances";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_OutInstances",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Counter",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE }, // <-- 이름은 유지(셰이더 호환)
					{ SHADER_TYPE_COMPUTE, "g_HeightMap",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_DensityField",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_InteractionField",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc linearWrap =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
				};
				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_LinearWrapSampler", linearWrap },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pGenCSO = renderer.AcquirePipelineState(psoCI, true);
				ASSERT(m_pGenCSO, "AcquireCompute(GrassGenerateInstances) failed.");

				m_pGenCSO->CreateShaderResourceBinding(&m_pGenSRB, true);
				ASSERT(m_pGenSRB, "GrassGenerateInstances SRB create failed.");

				// Bind stable resources (buffers / CB)
				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstances"))
				{
					var->Set(renderer.GetBufferUAV(kGrassInstanceBuffer));
				}
				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
				{
					// shared IndirectCountBuffer로 바인딩
					var->Set(renderer.GetBufferUAV(kIndirectCountBuffer));
				}
				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(kGrassGenCB));
				}
			});

		// =====================================================================
		// Pass 3) GrassForward (graphics, DrawIndexedIndirect)
		// =====================================================================
		renderer.AddPass(
			"GrassForward",
			[&](RenderPassBuilder& b)
			{
				b.DeclareTextureRTVReadWrite(kLightingTex);
				b.DeclareTextureDSVRead(kDepthTex);

				b.DeclareTextureSRVRead(kShadowMap);

				b.DeclareBufferSRVRead(kGrassInstanceBuffer);

				// Indirect args read (shared buffer)
				b.DeclareBufferIndirectArgsRead(kIndirectArgsBuffer);

				b.DeclareBufferCBVRead(kGrassRenderCB);
			},
			[this, kIndirectArgsBuffer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGrassPSO && m_pGrassSRB, "Grass PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				for (const DrawIndirectPacket& pkt : ctx.ForwardIndirectPackets)
				{
					pContext->SetPipelineState(m_pGrassPSO);
					pContext->CommitShaderResources(m_pGrassSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					IBuffer* ppVertexBuffers[] = { pkt.VertexBuffer };
					uint64 offsets[] = { 0 };

					pContext->SetVertexBuffers(
						0,
						1,
						ppVertexBuffers,
						offsets,
						RESOURCE_STATE_TRANSITION_MODE_VERIFY,
						SET_VERTEX_BUFFERS_FLAG_RESET);

					pContext->SetIndexBuffer(
						pkt.IndexBuffer,
						0,
						RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					pContext->DrawIndexedIndirect(pkt.DrawAttribs);
				}
			},
				[this, &renderer]()
			{
				GraphicsPipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Grass";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				auto& gp = psoCI.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

				gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
				gp.RasterizerDesc.FrontCounterClockwise = true;

				gp.DepthStencilDesc.DepthEnable = true;
				gp.DepthStencilDesc.DepthWriteEnable = true;
				gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "GrassVS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.Desc.UseCombinedTextureSamplers = false;
				vsCI.FilePath = m_GrassVS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "GrassPS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.Desc.UseCombinedTextureSamplers = false;
				psCI.FilePath = m_GrassPS.c_str();

				renderer.CreateShader(vsCI, &psoCI.pVS);
				renderer.CreateShader(psCI, &psoCI.pPS);
				ASSERT(psoCI.pVS && psoCI.pPS, "Grass VS/PS compile failed.");

				psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_BaseColorTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				psoCI.PSODesc.ResourceLayout.Variables = vars;
				psoCI.PSODesc.ResourceLayout.NumVariables = _countof(vars);

				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};

				SamplerDesc shadowClamp = {};
				shadowClamp.MinFilter = FILTER_TYPE_COMPARISON_LINEAR;
				shadowClamp.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
				shadowClamp.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
				shadowClamp.AddressU = TEXTURE_ADDRESS_CLAMP;
				shadowClamp.AddressV = TEXTURE_ADDRESS_CLAMP;
				shadowClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
				shadowClamp.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;

				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_PIXEL, "g_ShadowCmpSampler",   shadowClamp },
				};
				psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				static LayoutElement layoutElems[] =
				{
					LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
					LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
					LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
					LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
				};
				gp.InputLayout.LayoutElements = layoutElems;
				gp.InputLayout.NumElements = _countof(layoutElems);

				m_pGrassPSO = renderer.AcquirePipelineState(STRING_HASH("GrassForward"), psoCI, true);
				ASSERT(m_pGrassPSO, "AcquirePipelineState(GrassForward) failed.");

				if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances"))
				{
					var->Set(renderer.GetBufferSRV(STRING_HASH("GrassInstanceBuffer")));
				}
				if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
				}
				if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "GRASS_RENDER_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
				}

				m_pGrassPSO->CreateShaderResourceBinding(&m_pGrassSRB, true);
				ASSERT(m_pGrassSRB, "Grass SRB create failed.");
			});
		// =====================================================================
		// Pass X) GrassShadow (graphics, DrawIndexedIndirect into ShadowMap)
		// =====================================================================
		renderer.AddPass(
			"GrassShadow",
			[&](RenderPassBuilder& b)
			{
				const uint64 kShadowMap = STRING_HASH("ShadowMap");
				const uint64 kGrassInstanceBuffer = STRING_HASH("GrassInstanceBuffer");
				const uint64 kIndirectArgsBuffer = STRING_HASH("IndirectArgsBuffer");

				// Shadow map에 depth write (additive)
				b.DeclareTextureDSVWrite(kShadowMap);

				// Grass instances SRV read
				b.DeclareBufferSRVRead(kGrassInstanceBuffer);

				// Indirect args read -> RDG가 INDIRECT_ARGUMENT 전이 생성
				b.DeclareBufferIndirectArgsRead(kIndirectArgsBuffer);

				b.DeclareBufferSRVRead(STRING_HASH("DEP00"));

				// GrassShadow VS가 grass render cb를 쓰면 이것도
				// (GrassShadow.vsh에서 안 쓰면 빼도 됨)
				b.DeclareBufferCBVRead(STRING_HASH("GrassRenderConstantsCB"));
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGrassShadowPSO && m_pGrassShadowSRB, "GrassShadow PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// viewport to shadow map resolution
				Viewport vp = {};
				vp.Width = float(ctx.ShadowMapResolution);
				vp.Height = float(ctx.ShadowMapResolution);
				vp.MinDepth = 0.f;
				vp.MaxDepth = 1.f;
				pContext->SetViewports(1, &vp, 0, 0);

				const std::vector<DrawIndirectPacket>& packets = ctx.ShadowIndirectPackets;

				IPipelineState* pLastPSO = nullptr;
				IShaderResourceBinding* pLastSRB = nullptr;
				IBuffer* pLastVB = nullptr;
				IBuffer* pLastIB = nullptr;

				for (const DrawIndirectPacket& pktIn : packets)
				{
					ASSERT(pktIn.VertexBuffer && pktIn.IndexBuffer, "Invalid indirect packet VB/IB.");

					if (pLastPSO != m_pGrassShadowPSO)
					{
						pLastPSO = m_pGrassShadowPSO;
						pLastSRB = nullptr;
						pContext->SetPipelineState(pLastPSO);
					}

					if (pLastSRB != m_pGrassShadowSRB)
					{
						pLastSRB = m_pGrassShadowSRB;
						pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					}

					if (pLastVB != pktIn.VertexBuffer)
					{
						IBuffer* ppVB[] = { pktIn.VertexBuffer };
						uint64 offsets[] = { 0 };

						pContext->SetVertexBuffers(
							0, 1, ppVB, offsets,
							RESOURCE_STATE_TRANSITION_MODE_VERIFY,
							SET_VERTEX_BUFFERS_FLAG_RESET);

						pLastVB = pktIn.VertexBuffer;
					}

					if (pLastIB != pktIn.IndexBuffer)
					{
						pContext->SetIndexBuffer(pktIn.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
						pLastIB = pktIn.IndexBuffer;
					}

					DrawIndexedIndirectAttribs dia = pktIn.DrawAttribs;

					dia.pAttribsBuffer = ctx.pRegistry->GetBuffer(STRING_HASH("IndirectArgsBuffer"));

					dia.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

					pContext->DrawIndexedIndirect(dia);
				}
			},
				[this, &renderer]()
			{
				// ------------------------------------------------------------
				// Build PSO for GrassShadow
				//  - VS: GrassShadow.vsh
				//  - PS: Shadow.psh (depth-only)
				// ------------------------------------------------------------
				GraphicsPipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_GrassShadow";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				auto& gp = psoCI.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

				// Depth-only target: RTV 0, DSV is from render pass
				gp.NumRenderTargets = 0;
				gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
				gp.DSVFormat = TEX_FORMAT_UNKNOWN;

				gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
				gp.RasterizerDesc.FrontCounterClockwise = true;

				gp.DepthStencilDesc.DepthEnable = true;
				gp.DepthStencilDesc.DepthWriteEnable = true;
				gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

				static LayoutElement layoutElems[] =
				{
					LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
					LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
					LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
					LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
				};
				gp.InputLayout.LayoutElements = layoutElems;
				gp.InputLayout.NumElements = _countof(layoutElems);

				// Shaders
				{
					ShaderCreateInfo vsCI = {};
					vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					vsCI.EntryPoint = "main";
					vsCI.Desc.Name = "GrassShadow VS";
					vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
					vsCI.Desc.UseCombinedTextureSamplers = false;
					vsCI.FilePath = m_GrassShadowVS.c_str();

					ShaderCreateInfo psCI = {};
					psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					psCI.EntryPoint = "main";
					psCI.Desc.Name = "GrassShadow PS(Shadow.psh)";
					psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
					psCI.Desc.UseCombinedTextureSamplers = false;
					psCI.FilePath = m_ShadowPS.c_str();

					renderer.CreateShader(vsCI, &psoCI.pVS);
					renderer.CreateShader(psCI, &psoCI.pPS);
					ASSERT(psoCI.pVS && psoCI.pPS, "GrassShadow VS/PS compile failed.");
				}

				// Resource layout
				psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				m_pGrassShadowPSO = renderer.AcquirePipelineState(STRING_HASH("GrassShadow"), psoCI, true);
				ASSERT(m_pGrassShadowPSO, "AcquirePipelineState(GrassShadow) failed.");

				// ---- Bind static resources ----
				// Grass instances
				if (auto* var = m_pGrassShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances"))
				{
					var->Set(renderer.GetBufferSRV(STRING_HASH("GrassInstanceBuffer")));
				}

				// Shadow constants 
				if (auto* var = m_pGrassShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("SHADOW_CONSTANTS")));
				}

				// Grass render constants
				if (auto* var = m_pGrassShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
				}

				m_pGrassShadowPSO->CreateShaderResourceBinding(&m_pGrassShadowSRB, true);
				ASSERT(m_pGrassShadowSRB, "GrassShadow SRB create failed.");
			});
	}
} // namespace shz
