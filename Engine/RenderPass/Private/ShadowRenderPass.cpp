#include "pch.h"
#include "Engine/RenderPass/Public/ShadowRenderPass.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"

#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	ShadowRenderPass::ShadowRenderPass(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(ctx.pShaderSourceFactory, "Shader source factory is null.");

		// ------------------------------------------------------------
		// Create RenderPass + Framebuffer (depth-only)
		// ------------------------------------------------------------
		{
			// RenderPass
			{
				RenderPassAttachmentDesc at[1] = {};
				at[0].Format = TEX_FORMAT_D32_FLOAT;
				at[0].SampleCount = 1;
				at[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				at[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
				at[0].StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
				at[0].StencilStoreOp = ATTACHMENT_STORE_OP_DISCARD;
				at[0].InitialState = RESOURCE_STATE_DEPTH_WRITE;
				at[0].FinalState = RESOURCE_STATE_DEPTH_WRITE;

				AttachmentReference depthRef = {};
				depthRef.AttachmentIndex = 0;
				depthRef.State = RESOURCE_STATE_DEPTH_WRITE;

				SubpassDesc sp = {};
				sp.RenderTargetAttachmentCount = 0;
				sp.pDepthStencilAttachment = &depthRef;

				RenderPassDesc rp = {};
				rp.Name = "RP_Shadow";
				rp.AttachmentCount = 1;
				rp.pAttachments = at;
				rp.SubpassCount = 1;
				rp.pSubpasses = &sp;

				m_pRenderPass.Release();
				ctx.pDevice->CreateRenderPass(rp, &m_pRenderPass);
				ASSERT(m_pRenderPass, "CreateRenderPass(RP_Shadow) failed.");
			}

			// Framebuffer
			{
				ITextureView* atch[1] = { ctx.pRegistry->GetTextureDSV(STRING_HASH("ShadowMap")) };
				FramebufferDesc fb = {};
				fb.Name = "FB_Shadow";
				fb.pRenderPass = m_pRenderPass;
				fb.AttachmentCount = 1;
				fb.ppAttachments = atch;

				m_pFramebuffer.Release();
				ctx.pDevice->CreateFramebuffer(fb, &m_pFramebuffer);
				ASSERT(m_pFramebuffer, "CreateFramebuffer(FB_Shadow) failed.");
			}
		}
	}

	ShadowRenderPass::~ShadowRenderPass()
	{
		m_pFramebuffer.Release();
		m_pRenderPass.Release();
	}

	void ShadowRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
		m_DrawCallCount = 0;
	}

	void ShadowRenderPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "Context is null.");

		IDeviceContext* pContext = ctx.pImmediateContext;

		const std::vector<DrawPacket>& packets = ctx.ShadowDrawPackets;

		// To DEPTH_WRITE
		{
			StateTransitionDesc tr =
			{
				ctx.pRegistry->GetTexture(STRING_HASH("ShadowMap")),
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_DEPTH_WRITE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			pContext->TransitionResourceStates(1, &tr);
		}

		Viewport vp = {};
		vp.Width = float(ctx.ShadowMapResolution);
		vp.Height = float(ctx.ShadowMapResolution);
		vp.MinDepth = 0.f;
		vp.MaxDepth = 1.f;
		pContext->SetViewports(1, &vp, 0, 0);

		OptimizedClearValue clearVals[1] = {};
		clearVals[0].DepthStencil.Depth = 1.f;
		clearVals[0].DepthStencil.Stencil = 0;

		BeginRenderPassAttribs rp = {};
		rp.pRenderPass = m_pRenderPass;
		rp.pFramebuffer = m_pFramebuffer;
		rp.ClearValueCount = 1;
		rp.pClearValues = clearVals;

		pContext->BeginRenderPass(rp);

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

			if (pkt.DrawCallType == EDrawCallType::Direct)
			{
				DrawIndexedAttribs dia = pkt.DrawAttribs;
				{
					MapHelper<hlsl::DrawConstants> map(pContext, ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")), MAP_WRITE, MAP_FLAG_DISCARD);
					hlsl::DrawConstants* dst = map;

					dst->StartInstanceLocation = dia.FirstInstanceLocation;
				}

				pContext->DrawIndexed(dia);
			}
			else if (pkt.DrawCallType == EDrawCallType::Indirect)
			{
				DrawIndexedIndirectAttribs dia = pkt.DrawIndirectAttribs;
				pContext->DrawIndexedIndirect(dia);
			}
			else
			{
				ASSERT(false, "Unsupported draw call type.");
			}
#ifdef PROFILING
			++m_DrawCallCount;
#endif
		}

		pContext->EndRenderPass();

		// Shadow -> SRV
		{
			StateTransitionDesc tr2 =
			{
				ctx.pRegistry->GetTexture(STRING_HASH("ShadowMap")),
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			pContext->TransitionResourceStates(1, &tr2);
		}
	}

	void ShadowRenderPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void ShadowRenderPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
		// Shadow map is not swapchain-backed.
	}

	void ShadowRenderPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		(void)ctx;
		(void)width;
		(void)height;
	}
} // namespace shz
