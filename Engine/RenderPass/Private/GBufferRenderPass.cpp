#include "pch.h"
#include "Engine/RenderPass/Public/GBufferRenderPass.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/Renderer/Public/CommonResourceId.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	GBufferRenderPass::GBufferRenderPass(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pImmediateContext, "Context is null.");

		const uint32 w = (ctx.BackBufferWidth != 0) ? ctx.BackBufferWidth : 1;
		const uint32 h = (ctx.BackBufferHeight != 0) ? ctx.BackBufferHeight : 1;

		bool ok = false;

		ok = createPassObjects(ctx);
		ASSERT(ok, "Failed to create g-buffer pass objects.");

	}

	GBufferRenderPass::~GBufferRenderPass()
	{
		m_pFramebuffer.Release();
		m_pRenderPass.Release();
	}

	void GBufferRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
		m_DrawCallCount = 0;
	}

	void GBufferRenderPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "Context is null.");

		IDeviceContext* pContext = ctx.pImmediateContext;

		const std::vector<DrawPacket>& packets = ctx.GBufferDrawPackets;

		// RT/DS transitions
		{
			StateTransitionDesc tr[] =
			{
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer0_Albedo")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer1_Normal")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer2_MRAO")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer3_Emissive")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBufferDepth")),      RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_DEPTH_WRITE,   STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(tr), tr);

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

			pContext->BeginRenderPass(rp);
		}

		IPipelineState* pLastPSO = nullptr;
		IShaderResourceBinding* pLastSRB = nullptr;
		IBuffer* pLastVB = nullptr;
		IBuffer* pLastIB = nullptr;

		for (const DrawPacket& pkt : packets)
		{
			ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

			// Bind PSO
			if (pLastPSO != pkt.PSO)
			{
				pLastPSO = pkt.PSO;
				pLastSRB = nullptr;
				pContext->SetPipelineState(pLastPSO);
			}

			// Bind SRB
			if (pLastSRB != pkt.SRB)
			{
				pLastSRB = pkt.SRB;
				pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			}

			// VB/IB binding (ONLY mesh VB)
			if (pLastVB != pkt.VertexBuffer)
			{
				IBuffer* ppVertexBuffers[] = { pkt.VertexBuffer };
				uint64 pOffsets[] = { 0 };

				pContext->SetVertexBuffers(
					0,
					1,
					ppVertexBuffers,
					pOffsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pLastVB = pkt.VertexBuffer;
			}

			if (pLastIB != pkt.IndexBuffer)
			{
				pContext->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
				pLastIB = pkt.IndexBuffer;
			}

			// Per-draw: StartInstanceLocation -> DrawCB
			DrawIndexedAttribs dia = pkt.DrawAttribs;
#ifdef SHZ_DEBUG
			if (dia.Flags == DRAW_FLAG_NONE) dia.Flags = DRAW_FLAG_VERIFY_ALL;
#endif
			{
				MapHelper<hlsl::DrawConstants> map(pContext, ctx.pRegistry->GetBuffer(kRes_DrawCB), MAP_WRITE, MAP_FLAG_DISCARD);
				hlsl::DrawConstants* dst = map;

				dst->StartInstanceLocation = dia.FirstInstanceLocation;
			}

			pContext->DrawIndexed(dia);
#ifdef PROFILING
			++m_DrawCallCount;
#endif
		}

		pContext->EndRenderPass();

		// Outputs -> SRV
		{
			StateTransitionDesc tr2[] =
			{
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer0_Albedo")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer1_Normal")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer2_MRAO")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBuffer3_Emissive")), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pRegistry->GetTexture(STRING_HASH("GBufferDepth")),      RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(tr2), tr2);
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

		// FB rebind
		(void)createPassObjects(ctx);
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

			ITextureView* atch[5] =
			{
				ctx.pRegistry->GetTextureRTV(STRING_HASH("GBuffer0_Albedo")),
				ctx.pRegistry->GetTextureRTV(STRING_HASH("GBuffer1_Normal")),
				ctx.pRegistry->GetTextureRTV(STRING_HASH("GBuffer2_MRAO")),
				ctx.pRegistry->GetTextureRTV(STRING_HASH("GBuffer3_Emissive")),
				ctx.pRegistry->GetTextureDSV(STRING_HASH("GBufferDepth"))
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
} // namespace shz
