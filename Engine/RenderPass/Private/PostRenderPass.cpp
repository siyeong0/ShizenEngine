#include "pch.h"
#include "Engine/RenderPass/Public/PostRenderPass.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	bool PostRenderPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pImmediateContext, "Context is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");

		// Create render pass
		{
			ASSERT(!m_pRenderPass, "Render pass is already initialized.");

			const SwapChainDesc& scDesc = ctx.pSwapChain->GetDesc();

			RenderPassAttachmentDesc attachments[1] = {};
			attachments[0].Format = scDesc.ColorBufferFormat;
			attachments[0].SampleCount = 1;
			attachments[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			attachments[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
			attachments[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
			attachments[0].FinalState = RESOURCE_STATE_RENDER_TARGET;

			AttachmentReference colorRef = {};
			colorRef.AttachmentIndex = 0;
			colorRef.State = RESOURCE_STATE_RENDER_TARGET;

			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = 1;
			subpass.pRenderTargetAttachments = &colorRef;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = "RP_Post";
			rpDesc.AttachmentCount = 1;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			ctx.pDevice->CreateRenderPass(rpDesc, &m_pRenderPass);
			ASSERT(m_pRenderPass, "CreateRenderPass(RP_Post) failed.");
		}

		// Create PSO
		{
			ASSERT(!m_pPSO && !m_pSRB, "PSO/SRB is already initialized.");

			GraphicsPipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc.Name = "Post Copy PSO";
			psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

			GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
			gp.pRenderPass = m_pRenderPass;
			gp.SubpassIndex = 0;

			// Render targets defined by render pass
			gp.NumRenderTargets = 0;
			gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
			gp.DSVFormat = TEX_FORMAT_UNKNOWN;

			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
			gp.RasterizerDesc.FrontCounterClockwise = true;
			gp.DepthStencilDesc.DepthEnable = false;

			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.EntryPoint = "main";
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

			RefCntAutoPtr<IShader> vs;
			{
				sci.Desc = {};
				sci.Desc.Name = "PostCopy VS";
				sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
				sci.FilePath = m_VS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;

				ctx.pDevice->CreateShader(sci, &vs);
				if (!vs)
				{
					ASSERT(false, "Failed to create PostCopy VS.");
					return false;
				}
			}

			RefCntAutoPtr<IShader> ps;
			{
				sci.Desc = {};
				sci.Desc.Name = "PostCopy PS";
				sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
				sci.FilePath = m_PS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;

				ctx.pDevice->CreateShader(sci, &ps);
				if (!ps)
				{
					ASSERT(false, "Failed to create PostCopy PS.");
					return false;
				}
			}

			psoCi.pVS = vs;
			psoCi.pPS = ps;

			psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_PIXEL, "g_InputColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			};
			psoCi.PSODesc.ResourceLayout.Variables = vars;
			psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

			SamplerDesc linearClamp =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
			};

			ImmutableSamplerDesc samplers[] =
			{
				{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
			};
			psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
			psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

			m_pPSO = ctx.pPipelineStateManager->AcquireGraphics(psoCi);
			if (!m_pPSO)
			{
				ASSERT(false, "Failed to create Post PSO.");
				return false;
			}

			m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
			if (!m_pSRB)
			{
				ASSERT(false, "Failed to create SRB_Post.");
				return false;
			}
		}

		return true;
	}

	void PostRenderPass::Cleanup()
	{
		m_pFramebufferCurrentBB.Release();
		m_pRenderPass.Release();

		m_pSRB.Release();
		m_pPSO.Release();
	}

	void PostRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)buildFramebufferForCurrentBackBuffer(ctx);
	}

	void PostRenderPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "Context is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(m_pRenderPass, "Post RenderPass is null.");
		ASSERT(m_pFramebufferCurrentBB, "Post Framebuffer(CurrentBB) is null.");
		ASSERT(m_pPSO, "Post PSO is null.");
		ASSERT(m_pSRB, "Post SRB is null.");

		IDeviceContext* devCtx = ctx.pImmediateContext;
		ISwapChain* sc = ctx.pSwapChain;

		// Viewport: use swapchain desc
		{
			const SwapChainDesc& scDesc = sc->GetDesc();

			Viewport bbVp = {};
			bbVp.TopLeftX = 0;
			bbVp.TopLeftY = 0;
			bbVp.Width = float(scDesc.Width);
			bbVp.Height = float(scDesc.Height);
			bbVp.MinDepth = 0.f;
			bbVp.MaxDepth = 1.f;
			devCtx->SetViewports(1, &bbVp, 0, 0);
		}

		// Bind SRV
		ASSERT(m_pSRB, "Post SRB is null.");
		ASSERT(ctx.pLightingSrv, "Lighting SRV is null (post input).");

		if (auto* v = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor"))
		{
			v->Set(ctx.pLightingSrv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}

		// Transition backbuffer texture to RT
		{
			ITextureView* bbRtv = sc->GetCurrentBackBufferRTV();
			ASSERT(bbRtv, "Backbuffer RTV is null.");

			StateTransitionDesc tr =
			{
				bbRtv->GetTexture(),
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_RENDER_TARGET,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			devCtx->TransitionResourceStates(1, &tr);
		}

		OptimizedClearValue cv[1] = {};
		cv[0].Color[0] = 0.f;
		cv[0].Color[1] = 0.f;
		cv[0].Color[2] = 0.f;
		cv[0].Color[3] = 1.f;

		BeginRenderPassAttribs rp = {};
		rp.pRenderPass = m_pRenderPass;
		rp.pFramebuffer = m_pFramebufferCurrentBB;
		rp.ClearValueCount = 1;
		rp.pClearValues = cv;

		devCtx->BeginRenderPass(rp);
		devCtx->SetPipelineState(m_pPSO);
		devCtx->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		DrawAttribs da = {};
		da.NumVertices = 3;
		da.Flags = DRAW_FLAG_VERIFY_ALL;
		devCtx->Draw(da);

		devCtx->EndRenderPass();
	}

	void PostRenderPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
		m_pFramebufferCurrentBB.Release();
	}

	void PostRenderPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
		m_pFramebufferCurrentBB.Release();
	}

	void PostRenderPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		(void)ctx;
		(void)width;
		(void)height;

		m_pFramebufferCurrentBB.Release();
	}

	bool PostRenderPass::buildFramebufferForCurrentBackBuffer(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(m_pRenderPass, "Post render pass is null.");

		ITextureView* bbRtv = ctx.pSwapChain->GetCurrentBackBufferRTV();
		if (!bbRtv)
		{
			ASSERT(false, "Current backbuffer RTV is null.");
			return false;
		}

		FramebufferDesc fb = {};
		fb.Name = "FB_Post_CurrentBackBuffer";
		fb.pRenderPass = m_pRenderPass;
		fb.AttachmentCount = 1;
		fb.ppAttachments = &bbRtv;

		m_pFramebufferCurrentBB.Release();
		ctx.pDevice->CreateFramebuffer(fb, &m_pFramebufferCurrentBB);

		if (!m_pFramebufferCurrentBB)
		{
			ASSERT(false, "Failed to create post framebuffer for current backbuffer.");
			return false;
		}

		return true;
	}

} // namespace shz
