// Engine/RenderPass/Private/LightingRenderPass.cpp
#include "pch.h"
#include "Engine/RenderPass/Public/LightingRenderPass.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	bool LightingRenderPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pImmediateContext, "Context is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");
		ASSERT(ctx.pFrameCB, "FrameCB is null.");

		const uint32 w = (ctx.BackBufferWidth != 0) ? ctx.BackBufferWidth : 1;
		const uint32 h = (ctx.BackBufferHeight != 0) ? ctx.BackBufferHeight : 1;

		if (!createTargets(ctx, w, h))
			return false;

		if (!createPassObjects(ctx))
			return false;

		if (!createPSO(ctx))
			return false;

		bindInputs(ctx);
		return true;
	}

	void LightingRenderPass::Cleanup()
	{
		m_pSRB.Release();
		m_pPSO.Release();

		m_pFramebuffer.Release();
		m_pRenderPass.Release();

		m_pLightingTex.Release();
		m_pLightingRTV = nullptr;
		m_pLightingSRV = nullptr;

		m_Width = 0;
		m_Height = 0;
	}

	void LightingRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	bool LightingRenderPass::createTargets(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");

		const bool needRebuild =
			(m_Width != width) || (m_Height != height) || !m_pLightingTex;

		if (!needRebuild)
			return true;

		m_Width = width;
		m_Height = height;

		const SwapChainDesc& sc = ctx.pSwapChain->GetDesc();

		TextureDesc td = {};
		td.Name = "LightingColor";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = width;
		td.Height = height;
		td.MipLevels = 1;
		td.Format = sc.ColorBufferFormat;
		td.SampleCount = 1;
		td.Usage = USAGE_DEFAULT;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

		m_pLightingTex.Release();
		m_pLightingRTV = nullptr;
		m_pLightingSRV = nullptr;

		ctx.pDevice->CreateTexture(td, nullptr, &m_pLightingTex);
		ASSERT(m_pLightingTex, "Failed to create Lighting texture.");

		m_pLightingRTV = m_pLightingTex->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		m_pLightingSRV = m_pLightingTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		ASSERT(m_pLightingRTV && m_pLightingSRV, "Lighting RTV/SRV is null.");

		m_pFramebuffer.Release();
		return true;
	}

	bool LightingRenderPass::createPassObjects(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");

		const SwapChainDesc& scDesc = ctx.pSwapChain->GetDesc();

		// RenderPass (once)
		if (!m_pRenderPass)
		{
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
			rpDesc.Name = "RP_Lighting";
			rpDesc.AttachmentCount = 1;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			ctx.pDevice->CreateRenderPass(rpDesc, &m_pRenderPass);
			ASSERT(m_pRenderPass, "CreateRenderPass(RP_Lighting) failed.");
		}

		// Framebuffer (size-dependent)
		{
			ASSERT(m_pLightingRTV, "Lighting RTV is null.");

			ITextureView* atch[1] = { m_pLightingRTV };

			FramebufferDesc fbDesc = {};
			fbDesc.Name = "FB_Lighting";
			fbDesc.pRenderPass = m_pRenderPass;
			fbDesc.AttachmentCount = 1;
			fbDesc.ppAttachments = atch;

			m_pFramebuffer.Release();
			ctx.pDevice->CreateFramebuffer(fbDesc, &m_pFramebuffer);
			ASSERT(m_pFramebuffer, "CreateFramebuffer(FB_Lighting) failed.");
		}

		return true;
	}

	bool LightingRenderPass::createPSO(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");

		if (m_pPSO && m_pSRB)
			return true;

		GraphicsPipelineStateCreateInfo psoCi = {};
		psoCi.PSODesc.Name = "Lighting PSO";
		psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
		gp.pRenderPass = m_pRenderPass;
		gp.SubpassIndex = 0;

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
			sci.Desc.Name = "Lighting VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = m_VS.c_str();
			sci.Desc.UseCombinedTextureSamplers = false;
			ctx.pDevice->CreateShader(sci, &vs);
			ASSERT(vs, "Failed to create DeferredLighting VS.");
		}

		RefCntAutoPtr<IShader> ps;
		{
			sci.Desc = {};
			sci.Desc.Name = "Lighting PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = m_PS.c_str();
			sci.Desc.UseCombinedTextureSamplers = false;
			ctx.pDevice->CreateShader(sci, &ps);
			ASSERT(ps, "Failed to create DeferredLighting PS.");
		}

		psoCi.pVS = vs;
		psoCi.pPS = ps;

		psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc vars[] =
		{
			{ SHADER_TYPE_PIXEL, "g_GBuffer0",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer1",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer2",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer3",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBufferDepth", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_ShadowMap",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_EnvMapTex",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_IrradianceIBLTex",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_SpecularIBLTex",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_BrdfIBLTex",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		psoCi.PSODesc.ResourceLayout.Variables = vars;
		psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

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
		psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

		ctx.pDevice->CreateGraphicsPipelineState(psoCi, &m_pPSO);
		ASSERT(m_pPSO, "Lighting PSO create failed.");

		// Bind FRAME_CONSTANTS static
		if (auto* var = m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
			var->Set(ctx.pFrameCB);

		m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
		ASSERT(m_pSRB, "Lighting SRB create failed.");

		return true;
	}

	void LightingRenderPass::bindInputs(RenderPassContext& ctx)
	{
		if (!m_pSRB)
			return;

		auto setTex = [&](const char* name, ITextureView* srv)
		{
			if (auto var = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, name))
				var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		};

		setTex("g_GBuffer0", ctx.pGBufferSrv[0]);
		setTex("g_GBuffer1", ctx.pGBufferSrv[1]);
		setTex("g_GBuffer2", ctx.pGBufferSrv[2]);
		setTex("g_GBuffer3", ctx.pGBufferSrv[3]);
		setTex("g_GBufferDepth", ctx.pDepthSrv);
		setTex("g_ShadowMap", ctx.pShadowMapSrv);

		if (auto var = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvMapTex"))
		{
			if (ctx.pEnvTex)
				var->Set(ctx.pEnvTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}
		if (auto var = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceIBLTex"))
		{
			if (ctx.pEnvDiffuseTex)
				var->Set(ctx.pEnvDiffuseTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}
		if (auto var = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SpecularIBLTex"))
		{
			if (ctx.pEnvSpecularTex)
				var->Set(ctx.pEnvSpecularTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}
		if (auto var = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BrdfIBLTex"))
		{
			if (ctx.pEnvBrdfTex)
				var->Set(ctx.pEnvBrdfTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}
	}

	void LightingRenderPass::Execute(RenderPassContext& ctx, RenderScene& scene, const ViewFamily& viewFamily)
	{
		(void)scene;
		(void)viewFamily;

		ASSERT(ctx.pImmediateContext, "Context is null.");

		bindInputs(ctx);

		IDeviceContext* pCtx = ctx.pImmediateContext;

		// ensure inputs are current (wirePassOutputs가 매 프레임 뒤에 불려도, 안전하게 한 번 더)
		bindInputs(ctx);

		auto drawFullScreenTriangle = [&]()
		{
			DrawAttribs da = {};
			da.NumVertices = 3;
			da.Flags = DRAW_FLAG_VERIFY_ALL;
			pCtx->Draw(da);
		};

		// PASS 2: Lighting - 그대로
		{
			StateTransitionDesc tr =
			{
				m_pLightingTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_RENDER_TARGET,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			pCtx->TransitionResourceStates(1, &tr);

			OptimizedClearValue cv[1] = {};
			cv[0].Color[0] = 0.f;
			cv[0].Color[1] = 0.f;
			cv[0].Color[2] = 0.f;
			cv[0].Color[3] = 1.f;

			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_pRenderPass;
			rp.pFramebuffer = m_pFramebuffer;
			rp.ClearValueCount = 1;
			rp.pClearValues = cv;

			pCtx->BeginRenderPass(rp);
			pCtx->SetPipelineState(m_pPSO);
			pCtx->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			drawFullScreenTriangle();
			pCtx->EndRenderPass();

			StateTransitionDesc tr2 =
			{
				m_pLightingTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			pCtx->TransitionResourceStates(1, &tr2);
		}
	}

	void LightingRenderPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void LightingRenderPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void LightingRenderPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		ASSERT(width != 0 && height != 0, "Invalid size.");

		if (!createTargets(ctx, width, height))
			return;

		(void)createPassObjects(ctx);
	}
} // namespace shz
