// ============================================================================
// GrassSystem.cpp
// ============================================================================
#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"

#include "Engine/RenderSystem/Public/IndirectArgsSystem.h"
#include "Engine/RenderSystem/Public/InteractionSystem.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	void GrassSystem::InstallPasses(
		Renderer& renderer,
		RenderScene& scene,
		IndirectArgsSystem& indirect,
		const InteractionSystem& interaction)
	{
		m_pInteractionSystem = &interaction;

		ASSERT(m_GrassDesc.pMeshLod0, "GrassDesc.pMeshLod0 is null");
		ASSERT(m_GrassDesc.pCrossMeshLod1, "GrassDesc.pCrossMeshLod1 is null");
		ASSERT(m_GrassDesc.pBillboardMeshLod2, "GrassDesc.pBillboardMeshLod2 is null");

		// ---------------------------------------------------------------------
		// Buffers (instances)
		// ---------------------------------------------------------------------
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
			bd.Mode = BUFFER_MODE_STRUCTURED;

			bd.Name = "GrassInstanceBufferLOD0";
			bd.ElementByteStride = sizeof(hlsl::GrassMeshInstance);
			bd.Size = MAX_NUM_GRASS_LOD0_INSTANCES * sizeof(hlsl::GrassMeshInstance);
			renderer.AddBuffer(STRING_HASH("GrassInstanceBufferLOD0"), bd);

			bd.Name = "GrassInstanceBufferLOD1";
			bd.ElementByteStride = sizeof(hlsl::GrassCrossPlaneInstance);
			bd.Size = MAX_NUM_GRASS_LOD1_INSTANCES * sizeof(hlsl::GrassCrossPlaneInstance);
			renderer.AddBuffer(STRING_HASH("GrassInstanceBufferLOD1"), bd);

			bd.Name = "GrassInstanceBufferLOD2";
			bd.ElementByteStride = sizeof(hlsl::GrassBillboardInstance);
			bd.Size = MAX_NUM_GRASS_LOD2_INSTANCES * sizeof(hlsl::GrassBillboardInstance);
			renderer.AddBuffer(STRING_HASH("GrassInstanceBufferLOD2"), bd);
		}

		// Allocate indirect slots
		m_IndirectSlotLOD0 = indirect.AllocateSlot("GrassLOD0");
		m_IndirectSlotLOD1 = indirect.AllocateSlot("GrassLOD1");
		m_IndirectSlotLOD2 = indirect.AllocateSlot("GrassLOD2");

		// Indirect templates
		{
			hlsl::IndirectArgsTemplate t = {};
			t.StartIndexLocation = 0;
			t.BaseVertexLocation = 0;
			t.StartInstanceLocation = 0;

			t.IndexCountPerInstance = m_GrassDesc.pMeshLod0->IndexCount;
			indirect.SetTemplate(m_IndirectSlotLOD0, t);

			t.IndexCountPerInstance = m_GrassDesc.pCrossMeshLod1->IndexCount;
			indirect.SetTemplate(m_IndirectSlotLOD1, t);

			t.IndexCountPerInstance = m_GrassDesc.pBillboardMeshLod2->IndexCount;
			indirect.SetTemplate(m_IndirectSlotLOD2, t);
		}

		// GrassGenConstantsCB (CS)
		{
			BufferDesc bd = {};
			bd.Name = "GrassGenConstantsCB";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			bd.Size = sizeof(hlsl::GrassGenConstants);

			renderer.AddBuffer(STRING_HASH("GrassGenConstantsCB"), bd);
		}

		// ---------------------------------------------------------------------
		// Register indirect objects to RenderScene (Deferred에서 그리기)
		// ---------------------------------------------------------------------
		{
			RenderScene::IndirectObjectDesc d = {};
			d.bCastShadow = true;

			d.PassKey = STRING_HASH("GBuffer");

			d.pMesh = m_GrassDesc.pMeshLod0;
			d.IndirectSlot = m_IndirectSlotLOD0;
			scene.AddIndirect(d);

			d.pMesh = m_GrassDesc.pCrossMeshLod1;
			d.IndirectSlot = m_IndirectSlotLOD1;
			scene.AddIndirect(d);

			d.pMesh = m_GrassDesc.pBillboardMeshLod2;
			d.IndirectSlot = m_IndirectSlotLOD2;
			scene.AddIndirect(d);
		}

		// =====================================================================
		// Pass 1) GrassGenerateInstances (compute)  << 이것만 유지 >>
		// =====================================================================
		renderer.AddPass(
			"GrassGenerateInstances",
			EPassExecutionDomain::OutsideRenderPass,
			[&](RenderPassBuilder& b)
			{
				// Outputs
				b.DeclareBufferUAV(STRING_HASH("GrassInstanceBufferLOD0"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("GrassInstanceBufferLOD1"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("GrassInstanceBufferLOD2"), RENDER_ACCESS_WRITE);

				// Inputs
				b.DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));
				b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
				b.DeclareBufferUAV(STRING_HASH("IndirectCountBuffer"), RENDER_ACCESS_WRITE);

				// Constants
				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstantsCB"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGenCSO && m_pGenSRB, "GrassGenerate PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// Upload constants
				{
					MapHelper<hlsl::GrassGenConstants> map(
						pContext,
						ctx.pRegistry->GetBuffer(STRING_HASH("GrassGenConstantsCB")),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					map->IndirectSlotLOD0 = m_IndirectSlotLOD0;
					map->IndirectSlotLOD1 = m_IndirectSlotLOD1;
					map->IndirectSlotLOD2 = m_IndirectSlotLOD2;

					map->LOD0Distance = m_GrassDesc.LOD0Distance;
					map->LOD1Distance = m_GrassDesc.LOD1Distance;
					map->LodHysteresis = m_GrassDesc.LodHysteresis;

					map->YOffset = m_YOffset;
					map->ChunkSize = m_ChunkSize;
					map->ChunkHalfExtent = m_ChunkHalfExtent;
					map->SamplesPerChunk = m_SamplesPerChunk;
					map->Jitter = m_Jitter;
					map->MinPitch = m_MinPitch;
					map->MaxPitch = m_MaxPitch;
					map->MinScale = m_MinScale;
					map->MaxScale = m_MaxScale;
					map->SpawnProb = m_SpawnProb;
					map->SpawnRadius = m_SpawnRadius;
					map->BendStrengthMin = m_BendStrengthMin;
					map->BendStrengthMax = m_BendStrengthMax;
					map->SeedSalt = m_SeedSalt;
					map->DensityTiling = m_DensityTiling;
					map->DensityContrast = m_DensityContrast;
					map->DensityPow = m_DensityPow;
					map->SlopeToDensity = m_SlopeToDensity;
					map->HeightMinN = m_HeightMinN;
					map->HeightMaxN = m_HeightMaxN;
					map->HeightFadeN = m_HeightFadeN;

					map->InteractionInvWorldSizeXZ = float2{ 1.0f, 1.0f } / m_pInteractionSystem->GetWorldSizeXZ();
					map->InteractionOriginXZ = m_pInteractionSystem->GetWorldOriginXZ();

					const uint interactionResolution = m_pInteractionSystem->GetInteractionFieldResolution();
					const uint2 interactionTexelOrigin = m_pInteractionSystem->GetTexelOrigin();
					map->InteractionTexelOrigin = interactionTexelOrigin;
					map->InteractionInvFieldSize = float2{ 1.0f / interactionResolution, 1.0f / interactionResolution };
				}

				// Bind per-frame textures
				{
					StateTransitionDesc tr =
					{
						renderer.GetTexture(STRING_HASH("HeightField")),
						RESOURCE_STATE_UNKNOWN,
						RESOURCE_STATE_SHADER_RESOURCE,
						STATE_TRANSITION_FLAG_UPDATE_STATE
					};
					pContext->TransitionResourceStates(1, &tr);
				}

				// Dispatch
				{
					pContext->SetPipelineState(m_pGenCSO);
					pContext->CommitShaderResources(m_pGenSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

					DispatchComputeAttribs disp = {};
					disp.ThreadGroupCountX = (2u * m_ChunkHalfExtent + 8u - 1u) / 8u;
					disp.ThreadGroupCountY = (2u * m_ChunkHalfExtent + 8u - 1u) / 8u;
					disp.ThreadGroupCountZ = 1;

					pContext->DispatchCompute(disp);
				}
			},
				[this, &renderer]()
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
					{ SHADER_TYPE_COMPUTE, "g_CounterBuffer",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_DensityField",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_InteractionField", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				SamplerDesc linearWrap =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
				};
				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_COMPUTE, "g_LinearWrapSampler",  linearWrap  },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pGenCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pGenCSO, "AcquireCompute(GrassGenerateInstances) failed.");

				if (auto* pVar = m_pGenCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD0"))
				{
					pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD0")));
				}
				if (auto* pVar = m_pGenCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD1"))
				{
					pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD1")));
				}
				if (auto* pVar = m_pGenCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD2"))
				{
					pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD2")));
				}

				m_pGenCSO->CreateShaderResourceBinding(&m_pGenSRB, true);
				ASSERT(m_pGenSRB, "GrassGenerateInstances SRB create failed.");

				if (auto* pVar = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_CounterBuffer"))
				{
					pVar->Set(renderer.GetBufferUAV(STRING_HASH("IndirectCountBuffer")));
				}
				if (auto* pVar = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					pVar->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstantsCB")));
				}

				if (auto* pVar = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightField"))
				{
					pVar->Set(renderer.GetTextureSRV(STRING_HASH("HeightField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}

				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
				{
					var->Set(renderer.GetTextureSRV(STRING_HASH("GrassDensityField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}

				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
				{
					var->Set(renderer.GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			});
	}

} // namespace shz
