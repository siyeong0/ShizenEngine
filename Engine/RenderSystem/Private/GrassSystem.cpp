#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

#include "Engine/AssetManager/Public/AssetManager.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/RHI/Interface/GraphicsTypes.h"

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

	void GrassSystem::InstallPasses(Renderer& renderer)
	{
		// Resource IDs (centralized)
		const uint64 kInteractionField = STRING_HASH("InteractionField");
		const uint64 kInteractionStamps = STRING_HASH("InteractionStampBuffer");
		const uint64 kInteractionConstantsCB = STRING_HASH("InteractionConstantsCB");

		const uint64 kGrassGenCB = STRING_HASH("GrassGenConstantsCB");
		const uint64 kGrassRenderCB = STRING_HASH("GrassRenderConstantsCB");

		const uint64 kGrassDensityField = STRING_HASH("GrassDensityField");

		const uint64 kGrassInstanceBuffer = STRING_HASH("GrassInstanceBuffer");
		const uint64 kGrassCounter = STRING_HASH("GrassCounter");
		const uint64 kGrassIndirectArgs = STRING_HASH("GrassIndirectArgs");

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

						// NOTE: you used fixed 1025/spacing(1,1)/center=1 in old pass.
						// If you want, read these from GrassGenConstantsCB later.
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

				// (D) Apply stamps (optional)
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
				// Create 2 compute PSOs + SRBs once
				// NOTE: This needs Renderer to have access to Device/ShaderFactory/PipelineStateManager internally.
				// You already do that in Deferred/Post code.

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
				b.DeclareBufferUAV(kGrassCounter, RENDER_ACCESS_WRITE);

				// Inputs
				b.DeclareTextureSRVRead(kGrassDensityField);
				b.DeclareTextureSRVRead(kInteractionField);

				// Constants
				b.DeclareBufferCBVRead(kGrassGenCB);
			},
			[this, kGrassCounter, kGrassDensityField, kInteractionField, kGrassInstanceBuffer, kGrassGenCB](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pScene, "Scene is null.");
				ASSERT(ctx.pScene->GetHeightMap(), "HeightMap is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGenCSO && m_pGenSRB, "GrassGenerate PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// (0) Reset counter (COPY_DEST -> UAV back)
				{
					const uint32 zero = 0;
					pContext->UpdateBuffer(
						ctx.pRegistry->GetBuffer(kGrassCounter),
						0, sizeof(uint32),
						&zero,
						RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

					StateTransitionDesc trBack[] =
					{
						{
							ctx.pRegistry->GetBuffer(kGrassCounter),
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

					// External heightmap transition
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
				[this, &renderer, kGrassInstanceBuffer, kGrassCounter, kGrassGenCB]()
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
					{ SHADER_TYPE_COMPUTE, "g_Counter",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
					var->Set(renderer.GetBufferUAV(kGrassCounter));
				}
				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(kGrassGenCB));
				}
			});

		// =====================================================================
		// Pass 3) GrassWriteIndirectArgs (compute)
		// =====================================================================
		renderer.AddPass(
			"GrassWriteIndirectArgs",
			[&](RenderPassBuilder& b)
			{
				// Counter: read
				b.DeclareBufferUAV(kGrassCounter, RENDER_ACCESS_READ);
				// Args: write
				b.DeclareBufferUAV(kGrassIndirectArgs, RENDER_ACCESS_WRITE);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(m_pWriteArgsCSO && m_pWriteArgsSRB, "WriteArgs PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				pContext->SetPipelineState(m_pWriteArgsCSO);
				pContext->CommitShaderResources(m_pWriteArgsSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = 1;
				disp.ThreadGroupCountY = 1;
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer, kGrassCounter, kGrassIndirectArgs]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "WriteIndirectArgs";
				csCI.Desc.Name = "WriteIndirectArgsCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_WriteArgsCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "WriteIndirectArgsCS compile failed.");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_GrassWriteIndirectArgs";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_IndirectArgs", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Counter",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				psoCI.pCS = cs;

				m_pWriteArgsCSO = renderer.AcquirePipelineState(psoCI, true);
				ASSERT(m_pWriteArgsCSO, "AcquireCompute(WriteIndirectArgs) failed.");

				m_pWriteArgsCSO->CreateShaderResourceBinding(&m_pWriteArgsSRB, true);
				ASSERT(m_pWriteArgsSRB, "WriteIndirectArgs SRB create failed.");

				if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectArgs"))
				{
					var->Set(renderer.GetBufferUAV(kGrassIndirectArgs));
				}
				if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
				{
					var->Set(renderer.GetBufferUAV(kGrassCounter));
				}
			});

		// =====================================================================
		// Pass 4) GrassForward (graphics, DrawIndexedIndirect)
		// =====================================================================
		renderer.AddPass(
			"GrassForward",
			[&](RenderPassBuilder& b)
			{
				// Accumulate into Lighting => RTV readwrite (LOAD)
				b.DeclareTextureRTVReadWrite(kLightingTex);

				// Depth. TODO: 읽고 쓰지만 Lighting 보다 이후에 나오기 위해(Lighting은 읽기만 함)
				b.DeclareTextureDSVRead(kDepthTex);

				// Inputs
				b.DeclareTextureSRVRead(kShadowMap);

				// Ordering / bind
				b.DeclareBufferSRVRead(kGrassInstanceBuffer);
				b.DeclareBufferIndirectArgsRead(kGrassIndirectArgs);

				// Constants
				b.DeclareBufferCBVRead(kGrassRenderCB);

				// IMPORTANT:
				// - NO clear set here => should become LOAD in AddPass logic
			},
			[this, kGrassIndirectArgs](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGrassPSO && m_pGrassSRB, "Grass PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				pContext->SetPipelineState(m_pGrassPSO);
				pContext->CommitShaderResources(m_pGrassSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				// VB/IB

				IBuffer* ppVertexBuffers[] = { m_pGrassMesh->VertexBuffer };
				uint64 offsets[] = { 0 };

				pContext->SetVertexBuffers(
					0,
					1,
					ppVertexBuffers,
					offsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pContext->SetIndexBuffer(
					m_pGrassMesh->IndexBuffer,
					0,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				// Indirect draw
				DrawIndexedIndirectAttribs ia = {};
				ia.IndexType = m_pGrassMesh->IndexType;
				ia.pAttribsBuffer = ctx.pRegistry->GetBuffer(STRING_HASH("GrassIndirectArgs"));
				ia.DrawArgsOffset = 0;
				ia.DrawCount = 1;
				ia.DrawArgsStride = 20;
				ia.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;
				ia.pCounterBuffer = nullptr;
				ia.CounterOffset = 0;
				ia.CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;
				pContext->DrawIndexedIndirect(ia);
			},
				[this, &renderer]()
			{
				// Create Grass graphics PSO once (and SRB).
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

				// Shaders
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

				// Resource layout
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

				// Input layout (same as your old grass mesh)
				static LayoutElement layoutElems[] =
				{
					LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
					LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
					LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
					LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
				};
				gp.InputLayout.LayoutElements = layoutElems;
				gp.InputLayout.NumElements = _countof(layoutElems);

				// IMPORTANT: bind to pass renderpass via passId
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
	}
} // namespace shz
