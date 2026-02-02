#include "pch.h"
#include "Engine/RenderPass/Public/GrassGenerateInstancesPass.h"

#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	GrassGenerateInstancesPass::GrassGenerateInstancesPass()
	{
	}

	GrassGenerateInstancesPass::~GrassGenerateInstancesPass()
	{
		m_pCSRB.Release();
		m_pCSO.Release();
	}

	void GrassGenerateInstancesPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");

		// ------------------------------------------------------------
		// RenderGraph declarations
		// ------------------------------------------------------------
		{
			// Outputs
			DeclareBufferUAV(STRING_HASH("GrassInstanceBuffer"), RENDER_ACCESS_WRITE);
			DeclareBufferUAV(STRING_HASH("GrassCounter"), RENDER_ACCESS_WRITE);

			// Inputs
			DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));
			DeclareTextureSRVRead(STRING_HASH("InteractionField"));

			// Constants
			DeclareBufferCBVRead(STRING_HASH("GrassGenConstantsCB"));
		}

		// ------------------------------------------------------------
		// Compute PSO: GenerateGrassInstances
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.Name = "GrassGenerateInstancesCS";
			sci.EntryPoint = "GenerateGrassInstances";
			sci.FilePath = "GrassGenerateInstances.hlsl";

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

			psoCI.pCS = pCS;

			m_pCSO = ctx.pPipelineStateManager->AcquireCompute(psoCI);
			ASSERT(m_pCSO, "AcquireCompute(PSO_GrassGenerateInstances) failed.");

			m_pCSO->CreateShaderResourceBinding(&m_pCSRB, true);
			ASSERT(m_pCSRB, "Create SRB for GrassGenerateInstances failed.");

			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstances"))
				var->Set(ctx.pRegistry->GetBufferUAV(STRING_HASH("GrassInstanceBuffer")));

			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
				var->Set(ctx.pRegistry->GetBufferUAV(STRING_HASH("GrassCounter")));

			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				var->Set(ctx.pRegistry->GetBuffer(STRING_HASH("GrassGenConstantsCB")));
		}
	}

	void GrassGenerateInstancesPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassGenerateInstancesPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(ctx.pScene, "Scene is null.");
		ASSERT(ctx.pScene->GetHeightMap(), "HeightMap is null.");

		IDeviceContext* pContext = ctx.pImmediateContext;

		// ---------------------------------------------------------------------
		// (0) Reset counter (그리고 필요하면 여기서 args 초기화는 별도 pass에서)
		// ---------------------------------------------------------------------
		{
			const uint32 zero = 0;
			pContext->UpdateBuffer(
				ctx.pRegistry->GetBuffer(STRING_HASH("GrassCounter")),
				0,
				sizeof(uint32),
				&zero,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			// UpdateBuffer가 COPY_DEST로 바꿔놓을 수 있으니 UAV로 복귀
			StateTransitionDesc trBack[] =
			{
				{
					ctx.pRegistry->GetBuffer(STRING_HASH("GrassCounter")),
					RESOURCE_STATE_UNKNOWN,
					RESOURCE_STATE_UNORDERED_ACCESS,
					STATE_TRANSITION_FLAG_UPDATE_STATE
				},
			};
			pContext->TransitionResourceStates(_countof(trBack), trBack);
		}

		// ---------------------------------------------------------------------
		// (1) Bind per-frame inputs
		// ---------------------------------------------------------------------
		{
			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightMap"))
			{
				var->Set(ctx.pScene->GetHeightMap()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
					SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
			{
				var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("GrassDensityField")),
					SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
			{
				var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("InteractionField")),
					SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

			// External heightmap SRV transition (safe)
			StateTransitionDesc tr =
			{
				ctx.pScene->GetHeightMap(),
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			pContext->TransitionResourceStates(1, &tr);
		}

		// ---------------------------------------------------------------------
		// (2) Dispatch
		// ---------------------------------------------------------------------
		{
			pContext->SetPipelineState(m_pCSO);

			// 중요: VERIFY 말고 TRANSITION (Release 안정성)
			pContext->CommitShaderResources(m_pCSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			DispatchComputeAttribs disp = {};
			disp.ThreadGroupCountX = (2u * 32u + 8u - 1u) / 8u;
			disp.ThreadGroupCountY = (2u * 32u + 8u - 1u) / 8u;
			disp.ThreadGroupCountZ = 1;

			pContext->DispatchCompute(disp);
		}
	}

	void GrassGenerateInstancesPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassGenerateInstancesPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassGenerateInstancesPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		(void)ctx;
		(void)width;
		(void)height;
	}
} // namespace shz
