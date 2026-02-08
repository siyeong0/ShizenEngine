#include "pch.h"
#include "Engine/RenderSystem/Public/InteractionSystem.h"

#include <algorithm>

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

		// Interaction constants
		{
			BufferDesc bd = {};
			bd.Name = "InteractionConstantsCB";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			bd.Size = uint64(sizeof(hlsl::InteractionConstants));

			renderer.AddBuffer(STRING_HASH("InteractionConstantsCB"), bd);
		}

		renderer.AddPass(
			"GrassInteraction",
			[&](RenderPassBuilder& b)
			{
				// Interaction field RW
				b.DeclareTextureUAV(STRING_HASH("InteractionField"), RENDER_ACCESS_READWRITE);

				// Stamps upload + constants
				b.DeclareBufferSRVRead(STRING_HASH("InteractionStampBuffer"));
				b.DeclareBufferCBVRead(STRING_HASH("InteractionConstantsCB"));

				// HeightField constants (for pixel->world conversion)
				b.DeclareBufferCBVRead(STRING_HASH("HeightFieldConstantsCB"));
			},
			[this, &terrain](RenderPassContext& ctx)
			{
				(void)terrain;

				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pScene, "Scene is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pDecayCSO && m_pDecaySRB, "Decay PSO/SRB not ready.");
				ASSERT(m_pApplyCSO && m_pApplySRB, "Apply PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// ------------------------------------------------------------
				// (A) Upload stamps (WORLD space)
				// ------------------------------------------------------------
				uint32 stampCount = 0;
				{
					std::vector<hlsl::InteractionStamp> stampsWS;
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
				// (B) Upload constants
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

					// Tuning
					map->DecayPerSec = 0.35f;
					map->ClampMax = 1.0f;
					map->ClampMin = 0.0f;
					map->_Pad0 = 0.0f;
				}

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = DivUp(INTERACTION_FIELD_SIZE, THREAD_GROUP_SIZE_X);
				disp.ThreadGroupCountY = DivUp(INTERACTION_FIELD_SIZE, THREAD_GROUP_SIZE_Y);
				disp.ThreadGroupCountZ = 1;

				// ------------------------------------------------------------
				// (C) Decay
				// ------------------------------------------------------------
				{
					if (auto* var = m_pDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(
							ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")),
							SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					pContext->SetPipelineState(m_pDecayCSO);
					pContext->CommitShaderResources(m_pDecaySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					pContext->DispatchCompute(disp);
				}

				// ------------------------------------------------------------
				// (D) Apply stamps
				// ------------------------------------------------------------
				if (stampCount > 0)
				{
					if (auto* var = m_pApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					{
						var->Set(
							ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")),
							SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					pContext->SetPipelineState(m_pApplyCSO);
					pContext->CommitShaderResources(m_pApplySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					pContext->DispatchCompute(disp);
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

				// -----------------------------
				// (2) ApplyStamps PSO
				// -----------------------------
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

					psoCI.pCS = cs;

					m_pApplyCSO = renderer.AcquirePipelineState(psoCI, true);
					ASSERT(m_pApplyCSO, "AcquireCompute(InteractionApplyStamps) failed.");

					m_pApplyCSO->CreateShaderResourceBinding(&m_pApplySRB, true);
					ASSERT(m_pApplySRB, "InteractionApplyStamps SRB create failed.");

					if (auto* var = m_pApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
					{
						var->Set(renderer.GetBuffer(STRING_HASH("InteractionConstantsCB")));
					}

					// Stamps SRV
					if (auto* var = m_pApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Stamps"))
					{
						var->Set(renderer.GetBufferSRV(STRING_HASH("InteractionStampBuffer")));
					}
				}
			});
	}
} // namespace shz
