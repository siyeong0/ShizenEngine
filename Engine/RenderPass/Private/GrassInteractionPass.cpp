#include "pch.h"
#include "Engine/RenderPass/Public/GrassInteractionPass.h"

#include <algorithm>

#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	// World XZ -> Terrain UV (0..1), matches GrassBuildInstances.hlsl mapping assumption:
	// - heightfield size = (HFWidth-1)*SpacingX, (HFHeight-1)*SpacingZ
	// - if CenterXZ==1, terrain origin is centered: origin = -0.5*size
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

	GrassInteractionPass::GrassInteractionPass()
	{
	}

	GrassInteractionPass::~GrassInteractionPass()
	{
		m_pDecaySRB.Release();
		m_pDecayCSO.Release();

		m_pApplySRB.Release();
		m_pApplyCSO.Release();
	}

	void GrassInteractionPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");

		// ------------------------------------------------------------
		// RenderGraph declarations (Renderer auto-orders + auto-transitions)
		// ------------------------------------------------------------
		{
			// RW update target
			DeclareTextureUAV(STRING_HASH("InteractionField"), RENDER_ACCESS_READWRITE);

			// Input stamps + constants
			DeclareBufferSRVRead(STRING_HASH("InteractionStampBuffer"));
			DeclareBufferCBVRead(STRING_HASH("InteractionConstantsCB"));

			// We also use GrassGenConstantsCB on CPU for world->uv conversion (and shader may read it elsewhere)
			DeclareBufferCBVRead(STRING_HASH("GrassGenConstantsCB"));
		}

		// ------------------------------------------------------------
		// Compute PSO: Interaction Decay
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.Name = "InteractionDecayCS";
			sci.EntryPoint = "DecayInteractionField";
			sci.FilePath = "InteractionFieldUpdate.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(InteractionDecayCS) failed.");

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

			psoCI.pCS = pCS;

			m_pDecayCSO = ctx.pPipelineStateManager->AcquireCompute(psoCI);
			ASSERT(m_pDecayCSO, "AcquireCompute(PSO_InteractionDecay) failed.");

			m_pDecayCSO->CreateShaderResourceBinding(&m_pDecaySRB, true);
			ASSERT(m_pDecaySRB, "Create SRB for InteractionDecay failed.");

			if (auto* var = m_pDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(STRING_HASH("InteractionConstantsCB")));
			}
		}

		// ------------------------------------------------------------
		// Compute PSO: Interaction Apply Stamps
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.Name = "InteractionApplyStampsCS";
			sci.EntryPoint = "ApplyInteractionStamps";
			sci.FilePath = "InteractionFieldUpdate.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(InteractionApplyStampsCS) failed.");

			ComputePipelineStateCreateInfo psoCI = {};
			psoCI.PSODesc.Name = "PSO_InteractionApplyStamps";
			psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

			auto& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_Stamps",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

			psoCI.pCS = pCS;

			m_pApplyCSO = ctx.pPipelineStateManager->AcquireCompute(psoCI);
			ASSERT(m_pApplyCSO, "AcquireCompute(PSO_InteractionApplyStamps) failed.");

			m_pApplyCSO->CreateShaderResourceBinding(&m_pApplySRB, true);
			ASSERT(m_pApplySRB, "Create SRB for InteractionApplyStamps failed.");

			if (auto* var = m_pApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(STRING_HASH("InteractionConstantsCB")));
			}
			if (auto* var = m_pApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Stamps"))
			{
				var->Set(ctx.pRegistry->GetBufferSRV(STRING_HASH("InteractionStampBuffer")));
			}
		}
	}

	void GrassInteractionPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassInteractionPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(ctx.pScene, "Scene is null.");

		IDeviceContext* pContext = ctx.pImmediateContext;

		// ---------------------------------------------------------------------
		// (B) Upload stamps + InteractionConstants
		// ---------------------------------------------------------------------
		uint32 stampCount = 0;
		{
			MapHelper<hlsl::InteractionStamp> stampMap(
				pContext,
				ctx.pRegistry->GetBuffer(STRING_HASH("InteractionStampBuffer")),
				MAP_WRITE,
				MAP_FLAG_DISCARD);

			stampCount = 0;

			std::vector<hlsl::InteractionStamp> interactionStamps;
			ctx.pScene->ConsumeInteractionStamps(&interactionStamps);

			if (!interactionStamps.empty())
			{
				stampCount = (uint32)std::min<size_t>(interactionStamps.size(), MAX_NUM_INTERACTION_STAMPS);

				for (uint32 i = 0; i < stampCount; ++i)
				{
					hlsl::InteractionStamp s = interactionStamps[i];
					s.CenterXZ = WorldXZToTerrainUV(1025, 1025, 1.0f, 1.0f, 1, s.CenterXZ);
					s.Radius = WorldRadiusToUv_MinAxis(1025, 1025, 1.0f, 1.0f, s.Radius);

					stampMap[i] = s;
				}
			}
		}

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

			map->DecayPerSec = 0.15f;
			map->ClampMax = 1.0f;
			map->ClampMin = 0.0f;
			map->_Pad0 = 0.0f;
		}

		// ---------------------------------------------------------------------
		// (C) Dispatch: Decay -> ApplyStamps
		// NOTE: InteractionField 상태 전이는 Renderer가 Declare 기반으로 수행.
		// ---------------------------------------------------------------------
		auto divUp = [](uint32 x, uint32 d) -> uint32 { return (x + d - 1) / d; };

		DispatchComputeAttribs disp = {};
		disp.ThreadGroupCountX = divUp(INTERACTION_FIELD_SIZE, 8);
		disp.ThreadGroupCountY = divUp(INTERACTION_FIELD_SIZE, 8);
		disp.ThreadGroupCountZ = 1;

		// Decay
		{
			if (auto* var = m_pDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
			{
				var->Set(ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			pContext->SetPipelineState(m_pDecayCSO);
			pContext->CommitShaderResources(m_pDecaySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			pContext->DispatchCompute(disp);
		}

		// Apply stamps (optional)
		if (stampCount > 0)
		{
			if (auto* var = m_pApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
			{
				var->Set(ctx.pRegistry->GetTextureUAV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			pContext->SetPipelineState(m_pApplyCSO);
			pContext->CommitShaderResources(m_pApplySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			pContext->DispatchCompute(disp);
		}
	}

	void GrassInteractionPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassInteractionPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassInteractionPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		(void)ctx;
		(void)width;
		(void)height;
	}
} // namespace shz
