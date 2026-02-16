#include "pch.h"
#include "Engine/RenderSystem/Public/ShadowSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	void ShadowSystem::InstallPasses(Renderer& renderer)
	{
		renderer.AddPass(
			"Shadow",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				const uint64 kShadowMap = STRING_HASH("ShadowMap");

				// depth target write
				b.DeclareTextureDSVWrite(kShadowMap);

				b.DeclareBufferUAV(STRING_HASH("DEP00"), RENDER_ACCESS_WRITE);

				// clear
				b.SetClearDepthStencil(kShadowMap, 1.f, 0);
			},
			[](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");

				ctx.pRenderer->UpdateViewConstantBuffer(ctx.ShadowView);

				IDeviceContext* pContext = ctx.pImmediateContext;

				// viewport to shadow map resolution
				Viewport vp = {};
				vp.Width = float(ctx.ShadowMapResolution);
				vp.Height = float(ctx.ShadowMapResolution);
				vp.MinDepth = 0.f;
				vp.MaxDepth = 1.f;
				pContext->SetViewports(1, &vp, 0, 0);

				ASSERT(pContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");

				const std::vector<DrawPacket>& packets = ctx.ShadowDrawPackets;

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

					// VB/IB binding
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

					// Draw
					DrawIndexedAttribs dia = pkt.DrawAttribs;

					// DRAW_CONSTANTS update (StartInstanceLocation)
					{
						MapHelper<hlsl::DrawConstants> map(
							pContext,
							ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")),
							MAP_WRITE,
							MAP_FLAG_DISCARD);

						hlsl::DrawConstants* dst = map;
						dst->StartInstanceLocation = pkt.StartInstanceLocation;
					}

					pContext->DrawIndexed(dia);
				}
			});
	}
} // namespace shz
