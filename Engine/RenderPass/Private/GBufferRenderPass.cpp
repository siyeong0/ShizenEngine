// Engine/RenderPass/Private/GBufferRenderPass.cpp
#include "pch.h"
#include "Engine/RenderPass/Public/GBufferRenderPass.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"
#include "Engine/Renderer/Public/RendererMaterialStaticBinder.h"

namespace shz
{
	bool GBufferRenderPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pImmediateContext, "Context is null.");
		ASSERT(ctx.pCache, "Cache is null.");
		ASSERT(ctx.pObjectIndexVB, "ObjectIndexVB is null.");

		const uint32 w = (ctx.BackBufferWidth != 0) ? ctx.BackBufferWidth : 1;
		const uint32 h = (ctx.BackBufferHeight != 0) ? ctx.BackBufferHeight : 1;

		if (!createTargets(ctx, w, h))
			return false;

		if (!createPassObjects(ctx))
			return false;

		return true;
	}

	void GBufferRenderPass::Cleanup()
	{
		m_pFramebuffer.Release();
		m_pRenderPass.Release();

		for (uint32 i = 0; i < RenderPassContext::NUM_GBUFFERS; ++i)
		{
			m_pGBufferTex[i].Release();
			m_pGBufferRTV[i] = nullptr;
			m_pGBufferSRV[i] = nullptr;
		}

		m_pDepthTex.Release();
		m_pDepthDSV = nullptr;
		m_pDepthSRV = nullptr;

		m_Width = 0;
		m_Height = 0;
	}

	void GBufferRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	bool GBufferRenderPass::createTargets(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		IRenderDevice* device = ctx.pDevice;
		ASSERT(device, "Device is null.");

		const bool needRebuild =
			(m_Width != width) || (m_Height != height) ||
			!m_pGBufferTex[0] || !m_pGBufferTex[1] || !m_pGBufferTex[2] || !m_pGBufferTex[3] ||
			!m_pDepthTex;

		if (!needRebuild)
			return true;

		m_Width = width;
		m_Height = height;

		auto createRtTexture2d = [&](uint32 w, uint32 h, TEXTURE_FORMAT fmt, const char* name,
			RefCntAutoPtr<ITexture>& outTex, ITextureView*& outRtv, ITextureView*& outSrv)
		{
			TextureDesc td = {};
			td.Name = name;
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.Format = fmt;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

			outTex.Release();
			outRtv = nullptr;
			outSrv = nullptr;

			device->CreateTexture(td, nullptr, &outTex);
			ASSERT(outTex, "Failed to create RT texture.");

			outRtv = outTex->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
			outSrv = outTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
			ASSERT(outRtv && outSrv, "RTV/SRV is null.");
		};

		// same formats as old
		createRtTexture2d(width, height, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_AlbedoA", m_pGBufferTex[0], m_pGBufferRTV[0], m_pGBufferSRV[0]);
		createRtTexture2d(width, height, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_NormalWS", m_pGBufferTex[1], m_pGBufferRTV[1], m_pGBufferSRV[1]);
		createRtTexture2d(width, height, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO", m_pGBufferTex[2], m_pGBufferRTV[2], m_pGBufferSRV[2]);
		createRtTexture2d(width, height, TEX_FORMAT_RGBA16_FLOAT, "GBuffer3_Emissive", m_pGBufferTex[3], m_pGBufferRTV[3], m_pGBufferSRV[3]);

		// Depth
		{
			TextureDesc td = {};
			td.Name = "GBufferDepth";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = width;
			td.Height = height;
			td.MipLevels = 1;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.Format = TEX_FORMAT_R32_TYPELESS;
			td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

			m_pDepthTex.Release();
			m_pDepthDSV = nullptr;
			m_pDepthSRV = nullptr;

			device->CreateTexture(td, nullptr, &m_pDepthTex);
			ASSERT(m_pDepthTex, "Failed to create GBufferDepth texture.");

			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			vd.Format = TEX_FORMAT_D32_FLOAT;
			m_pDepthTex->CreateView(vd, &m_pDepthDSV);
			ASSERT(m_pDepthDSV, "Failed to create GBufferDepth DSV.");

			vd = {};
			vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			vd.Format = TEX_FORMAT_R32_FLOAT;
			m_pDepthTex->CreateView(vd, &m_pDepthSRV);
			ASSERT(m_pDepthSRV, "Failed to create GBufferDepth SRV.");
		}

		// FB will be recreated in createPassObjects
		m_pFramebuffer.Release();
		return true;
	}

	bool GBufferRenderPass::createPassObjects(RenderPassContext& ctx)
	{
		IRenderDevice* device = ctx.pDevice;
		ASSERT(device, "Device is null.");

		// RenderPass (once)
		if (!m_pRenderPass)
		{
			RenderPassAttachmentDesc attachments[5] = {};

			attachments[0].Format = TEX_FORMAT_RGBA8_UNORM;
			attachments[1].Format = TEX_FORMAT_RGBA16_FLOAT;
			attachments[2].Format = TEX_FORMAT_RGBA8_UNORM;
			attachments[3].Format = TEX_FORMAT_RGBA16_FLOAT;

			for (uint32 i = 0; i < 4; ++i)
			{
				attachments[i].SampleCount = 1;
				attachments[i].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				attachments[i].StoreOp = ATTACHMENT_STORE_OP_STORE;
				attachments[i].InitialState = RESOURCE_STATE_RENDER_TARGET;
				attachments[i].FinalState = RESOURCE_STATE_RENDER_TARGET;
			}

			attachments[4].Format = TEX_FORMAT_D32_FLOAT;
			attachments[4].SampleCount = 1;
			attachments[4].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			attachments[4].StoreOp = ATTACHMENT_STORE_OP_STORE;
			attachments[4].InitialState = RESOURCE_STATE_DEPTH_WRITE;
			attachments[4].FinalState = RESOURCE_STATE_DEPTH_WRITE;

			AttachmentReference colorRefs[4] = {};
			for (uint32 i = 0; i < 4; ++i)
			{
				colorRefs[i].AttachmentIndex = i;
				colorRefs[i].State = RESOURCE_STATE_RENDER_TARGET;
			}

			AttachmentReference depthRef = {};
			depthRef.AttachmentIndex = 4;
			depthRef.State = RESOURCE_STATE_DEPTH_WRITE;

			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = 4;
			subpass.pRenderTargetAttachments = colorRefs;
			subpass.pDepthStencilAttachment = &depthRef;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = "RP_GBuffer";
			rpDesc.AttachmentCount = 5;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			device->CreateRenderPass(rpDesc, &m_pRenderPass);
			ASSERT(m_pRenderPass, "CreateRenderPass(RP_GBuffer) failed.");
		}

		// Framebuffer (size-dependent)
		{
			ASSERT(m_pRenderPass, "RP_GBuffer is null.");
			ASSERT(m_pGBufferRTV[0] && m_pGBufferRTV[1] && m_pGBufferRTV[2] && m_pGBufferRTV[3] && m_pDepthDSV, "GBuffer views are null.");

			ITextureView* atch[5] =
			{
				m_pGBufferRTV[0],
				m_pGBufferRTV[1],
				m_pGBufferRTV[2],
				m_pGBufferRTV[3],
				m_pDepthDSV
			};

			FramebufferDesc fbDesc = {};
			fbDesc.Name = "FB_GBuffer";
			fbDesc.pRenderPass = m_pRenderPass;
			fbDesc.AttachmentCount = 5;
			fbDesc.ppAttachments = atch;

			m_pFramebuffer.Release();
			device->CreateFramebuffer(fbDesc, &m_pFramebuffer);
			ASSERT(m_pFramebuffer, "CreateFramebuffer(FB_GBuffer) failed.");
		}

		return true;
	}

	void GBufferRenderPass::Execute(RenderPassContext& ctx, RenderScene& scene, const ViewFamily& viewFamily)
	{
		ASSERT(ctx.pImmediateContext, "Context is null.");
		ASSERT(ctx.pCache, "Cache is null.");

		(void)viewFamily;

		IDeviceContext* pCtx = ctx.pImmediateContext;

		// PASS 1: GBuffer (material batching) - ±×´ë·Î
		{
			StateTransitionDesc tr[] =
			{
				{ m_pGBufferTex[0],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGBufferTex[1],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGBufferTex[2],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGBufferTex[3],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pDepthTex,        RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_DEPTH_WRITE,   STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pCtx->TransitionResourceStates(_countof(tr), tr);

			OptimizedClearValue clearVals[5] = {};
			for (int i = 0; i < 4; ++i)
			{
				clearVals[i].Color[0] = 0.f;
				clearVals[i].Color[1] = 0.f;
				clearVals[i].Color[2] = 0.f;
				clearVals[i].Color[3] = 0.f;
			}
			clearVals[4].DepthStencil.Depth = 1.f;
			clearVals[4].DepthStencil.Stencil = 0;

			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_pRenderPass;
			rp.pFramebuffer = m_pFramebuffer;
			rp.ClearValueCount = 5;
			rp.pClearValues = clearVals;

			pCtx->BeginRenderPass(rp);

			const MaterialRenderData* currMat = nullptr;

			const auto& objs = scene.GetObjects();
			for (uint32 objIndex = 0; objIndex < static_cast<uint32>(objs.size()); ++objIndex)
			{
				const auto& obj = objs[objIndex];

				const StaticMeshRenderData* mesh = ctx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				ctx.UploadObjectIndexInstance(objIndex);

				IBuffer* vbs[] = { mesh->GetVertexBuffer(), ctx.pObjectIndexVB };
				uint64 offs[] = { 0, 0 };
				pCtx->SetVertexBuffers(0, 2, vbs, offs,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pCtx->SetIndexBuffer(mesh->GetIndexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					if (sec.IndexCount == 0)
						continue;

					const MaterialInstance* inst = &obj.Materials[sec.MaterialSlot];
					if (!inst)
						continue;

					const uint64 key = reinterpret_cast<uint64>(inst);
					auto it = ctx.FrameMat.find(key);
					if (it == ctx.FrameMat.end())
						continue;

					MaterialRenderData* rd = ctx.pCache->TryGetMaterialRenderData(it->second);
					if (!rd || !rd->GetPSO() || !rd->GetSRB())
						continue;

					if (currMat != rd)
					{
						currMat = rd;

						pCtx->SetPipelineState(rd->GetPSO());
						pCtx->CommitShaderResources(rd->GetSRB(), RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					}

					DrawIndexedAttribs dia = {};
					dia.NumIndices = sec.IndexCount;
					dia.IndexType = indexType;
					dia.Flags = DRAW_FLAG_VERIFY_ALL;
					dia.FirstIndexLocation = sec.FirstIndex;
					dia.BaseVertex = static_cast<int32>(sec.BaseVertex);
					dia.NumInstances = 1;

					pCtx->DrawIndexed(dia);
				}
			}

			pCtx->EndRenderPass();

			StateTransitionDesc tr2[] =
			{
				{ m_pGBufferTex[0],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGBufferTex[1],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGBufferTex[2],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGBufferTex[3],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pDepthTex,        RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pCtx->TransitionResourceStates(_countof(tr2), tr2);
		}
	}

	void GBufferRenderPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GBufferRenderPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
		// Offscreen; no swapchain refs.
	}

	void GBufferRenderPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		ASSERT(width != 0 && height != 0, "Invalid size.");

		if (!createTargets(ctx, width, height))
			return;

		// FB rebind
		(void)createPassObjects(ctx);
	}
} // namespace shz
