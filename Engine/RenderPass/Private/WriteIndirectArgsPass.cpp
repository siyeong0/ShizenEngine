#include "pch.h"
#include "Engine/RenderPass/Public/WriteIndirectArgsPass.h"

#include "Engine/Renderer/Public/RenderResourceRegistry.h"

namespace shz
{
	WriteIndirectArgsPass::WriteIndirectArgsPass(uint64 counterID, uint64 indirectArgsID)
	{
		m_CounterID = counterID;
		m_IndirectArgsID = indirectArgsID;
	}

	WriteIndirectArgsPass::~WriteIndirectArgsPass()
	{
		m_pCSRB.Release();
		m_pCSO.Release();
	}

	void WriteIndirectArgsPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");

		// ------------------------------------------------------------
		// RenderGraph declarations
		// - Counter: 읽기(하지만 UAV로 바인딩해도 무방)
		// - IndirectArgs: 쓰기
		// ------------------------------------------------------------
		{
			// 그래프가 의존성/배리어를 만들 수 있게 선언
			DeclareBufferUAV(m_CounterID, RENDER_ACCESS_READ);
			DeclareBufferUAV(m_IndirectArgsID, RENDER_ACCESS_WRITE);
		}

		// ------------------------------------------------------------
		// Compute PSO: WriteIndirectArgs
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;
			sci.Desc.UseCombinedTextureSamplers = false;

			sci.Desc.Name = "WriteIndirectArgsCS";
			sci.EntryPoint = "WriteIndirectArgs";
			sci.FilePath = "WriteIndirectArgs.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(WriteIndirectArgsCS) failed.");

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

			m_pCSO = ctx.pPipelineStateManager->AcquireCompute(psoCI);
			ASSERT(m_pCSO, "AcquireCompute(PSO_WriteIndirectArgs) failed.");

			m_pCSO->CreateShaderResourceBinding(&m_pCSRB, true);
			ASSERT(m_pCSRB, "Create SRB for WriteIndirectArgs failed.");

			// 여기서 “ID만으로” 바인딩이 끝나야 함
			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectArgs"))
			{
				var->Set(ctx.pRegistry->GetBufferUAV(m_IndirectArgsID));
			}
			if (auto* var = m_pCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
			{
				// counter는 읽기만 해도 UAV로 바인딩해도 됨 (Load만 할거라)
				var->Set(ctx.pRegistry->GetBufferUAV(m_CounterID));
			}
		}
	}

	void WriteIndirectArgsPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void WriteIndirectArgsPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");

		IDeviceContext* pContext = ctx.pImmediateContext;

		// ---------------------------------------------------------------------
		// (0) Indirect args 초기값(예: IndexCountPerInstance=6)은 이전 단계에서 해두고
		//     여기서는 InstanceCount만 덮어쓰는 역할로 둠.
		//     (원하면 여기서 기본 args를 UpdateBuffer로 써도 됨)
		// ---------------------------------------------------------------------

		// ---------------------------------------------------------------------
		// (1) Dispatch
		// ---------------------------------------------------------------------
		{
			pContext->SetPipelineState(m_pCSO);

			// 중요: VERIFY 말고 TRANSITION
			pContext->CommitShaderResources(m_pCSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			DispatchComputeAttribs disp = {};
			disp.ThreadGroupCountX = 1;
			disp.ThreadGroupCountY = 1;
			disp.ThreadGroupCountZ = 1;

			pContext->DispatchCompute(disp);
		}
	}

	void WriteIndirectArgsPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void WriteIndirectArgsPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void WriteIndirectArgsPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		(void)ctx;
		(void)width;
		(void)height;
	}
} // namespace shz
