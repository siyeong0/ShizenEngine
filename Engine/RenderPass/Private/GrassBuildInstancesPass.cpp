#include "pch.h"
#include "Engine/RenderPass/Public/GrassBuildInstancesPass.h"

#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	GrassBuildInstancesPass::GrassBuildInstancesPass()
	{
	}

	GrassBuildInstancesPass::~GrassBuildInstancesPass()
	{
		m_pGenCSRB.Release();
		m_pGenCSO.Release();

		m_pArgsCSRB.Release();
		m_pArgsCSO.Release();
	}

	void GrassBuildInstancesPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");

		// ------------------------------------------------------------
		// RenderGraph declarations (Renderer auto-orders + auto-transitions)
		// ------------------------------------------------------------
		{
			// Outputs
			DeclareBufferUAV(STRING_HASH("GrassInstanceBuffer"), RENDER_ACCESS_WRITE);
			DeclareBufferUAV(STRING_HASH("GrassCounter"), RENDER_ACCESS_WRITE);
			DeclareBufferUAV(STRING_HASH("GrassIndirectArgs"), RENDER_ACCESS_WRITE);

			// Inputs
			DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));
			DeclareTextureSRVRead(STRING_HASH("InteractionField"));

			// Constants
			DeclareBufferCBVRead(STRING_HASH("GrassGenConstantsCB"));
		}

		// ------------------------------------------------------------
		// Compute PSO #1: GenerateGrassInstances
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.Name = "GrassGenerateInstancesCS";
			sci.EntryPoint = "GenerateGrassInstances";
			sci.FilePath = "GrassBuildInstances.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(GrassGenerateInstancesCS) failed.");

			ComputePipelineStateCreateInfo psoCI = {};
			psoCI.PSODesc.Name = "PSO_GrassGenerateInstances";
			psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

			auto& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_OutInstances",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_Counter",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_HeightMap",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_DensityField",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_InteractionField",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

			psoCI.pCS = pCS;

			m_pGenCSO = ctx.pPipelineStateManager->AcquireCompute(psoCI);
			ASSERT(m_pGenCSO, "AcquireCompute(PSO_GrassGenerateInstances) failed.");

			m_pGenCSO->CreateShaderResourceBinding(&m_pGenCSRB, true);
			ASSERT(m_pGenCSRB, "Create SRB for GrassGenerateInstances failed.");

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstances"))
			{
				var->Set(ctx.pRegistry->GetBufferUAV(STRING_HASH("GrassInstanceBuffer")));
			}

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
			{
				var->Set(ctx.pRegistry->GetBufferUAV(STRING_HASH("GrassCounter")));
			}

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(STRING_HASH("GrassGenConstantsCB")));
			}
		}

		// ------------------------------------------------------------
		// Compute PSO #2: WriteIndirectArgs
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;
			sci.Desc.UseCombinedTextureSamplers = false;

			sci.Desc.Name = "GrassWriteIndirectArgsCS";
			sci.EntryPoint = "WriteIndirectArgs";
			sci.FilePath = "GrassBuildInstances.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(GrassWriteIndirectArgsCS) failed.");

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

			psoCI.pCS = pCS;

			m_pArgsCSO = ctx.pPipelineStateManager->AcquireCompute(psoCI);
			ASSERT(m_pArgsCSO, "AcquireCompute(PSO_GrassWriteIndirectArgs) failed.");

			m_pArgsCSO->CreateShaderResourceBinding(&m_pArgsCSRB, true);
			ASSERT(m_pArgsCSRB, "Create SRB for GrassWriteIndirectArgs failed.");

			if (auto* var = m_pArgsCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectArgs"))
			{
				var->Set(ctx.pRegistry->GetBufferUAV(STRING_HASH("GrassIndirectArgs")));
			}
			if (auto* var = m_pArgsCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
			{
				var->Set(ctx.pRegistry->GetBufferUAV(STRING_HASH("GrassCounter")));
			}
		}
	}

	void GrassBuildInstancesPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassBuildInstancesPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(ctx.pScene, "Scene is null.");
		ASSERT(ctx.pScene->GetHeightMap(), "HeightMap is null.");

		IDeviceContext* pContext = ctx.pImmediateContext;

		// ---------------------------------------------------------------------
		// (0) Reset counter + init indirect args
		// NOTE: 이 UpdateBuffer는 내부적으로 전이 처리함 (TRANSITION)
		// ---------------------------------------------------------------------
		{
			const uint32 zero = 0;
			pContext->UpdateBuffer(
				ctx.pRegistry->GetBuffer(STRING_HASH("GrassCounter")),
				0,
				sizeof(uint32),
				&zero,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			const uint32 args[5] = { 6u, 0u, 0u, 0u, 0u };
			pContext->UpdateBuffer(
				ctx.pRegistry->GetBuffer(STRING_HASH("GrassIndirectArgs")),
				0,
				sizeof(args),
				args,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			// UpdateBuffer가 COPY_DEST로 바꿔놨으니, CS 전에 UAV로 다시 전이!
			StateTransitionDesc trBack[] =
			{
				{ ctx.pRegistry->GetBuffer(STRING_HASH("GrassCounter")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS,STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetBuffer(STRING_HASH("GrassIndirectArgs")),RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS,STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(trBack), trBack);
		}

		// ---------------------------------------------------------------------
		// (1) Compute: GenerateGrassInstances
		// NOTE: Registry 리소스 상태 전이는 Renderer가 Declare 기반으로 수행.
		//       HeightMap은 외부이므로 여기서 SRV 전이만 안전하게 처리.
		// ---------------------------------------------------------------------
		{
			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightMap"))
			{
				var->Set(ctx.pScene->GetHeightMap()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
			{
				var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("GrassDensityField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
			{
				var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			// External heightmap SRV transition (safe)
			{
				StateTransitionDesc tr =
				{
					ctx.pScene->GetHeightMap(),
					RESOURCE_STATE_UNKNOWN,
					RESOURCE_STATE_SHADER_RESOURCE,
					STATE_TRANSITION_FLAG_UPDATE_STATE
				};
				pContext->TransitionResourceStates(1, &tr);
			}

			pContext->SetPipelineState(m_pGenCSO);
			pContext->CommitShaderResources(m_pGenCSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			DispatchComputeAttribs disp = {};
			disp.ThreadGroupCountX = (2u * 32u + 8u - 1u) / 8u;
			disp.ThreadGroupCountY = (2u * 32u + 8u - 1u) / 8u;
			disp.ThreadGroupCountZ = 1;

			pContext->DispatchCompute(disp);
		}

		// ---------------------------------------------------------------------
		// (2) Compute: WriteIndirectArgs
		// ---------------------------------------------------------------------
		{
			pContext->SetPipelineState(m_pArgsCSO);
			pContext->CommitShaderResources(m_pArgsCSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			DispatchComputeAttribs disp = {};
			disp.ThreadGroupCountX = 1;
			disp.ThreadGroupCountY = 1;
			disp.ThreadGroupCountZ = 1;

			pContext->DispatchCompute(disp);
		}
	}

	void GrassBuildInstancesPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassBuildInstancesPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassBuildInstancesPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		(void)ctx;
		(void)width;
		(void)height;
	}
} // namespace shz
