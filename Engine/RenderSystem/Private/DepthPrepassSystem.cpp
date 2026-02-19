#include "pch.h"
#include "Engine/RenderSystem/Public/DepthPrepassSystem.h"

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

	void DepthPrepassSystem::Initialize(Renderer& renderer)
	{

	}

	void DepthPrepassSystem::InstallPasses(Renderer& renderer)
	{
		// ------------------------------------------------------------
		// Pass: DepthPrepass (writes GBufferDepth only)
		// ------------------------------------------------------------
		renderer.AddPass(
			"DepthPrepass",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				const uint64 kDepth = STRING_HASH("GBufferDepth");

				// Depth-only
				b.DeclareTextureDSVWrite(kDepth);

				// If you want to use indirect for some geometry in depth-only, declare it
				b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectArgsBuffer"));
				b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectDrawCountBuffer"));

				// Clear depth at frame begin
				b.SetClearDepthStencil(kDepth, 1.f, 0);
			},
			[](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				IPipelineState* pLastPSO = nullptr;
				IShaderResourceBinding* pLastSRB = nullptr;
				IBuffer* pLastVB = nullptr;
				IBuffer* pLastIB = nullptr;

				// -------------------------
				// Direct packets
				// -------------------------
				for (const DrawPacket& pkt : ctx.DepthPrepassDrawPackets)
				{
					ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

					IPipelineState* pPSO = pkt.PSO;
					IShaderResourceBinding* pSRB = pkt.SRB;

					ASSERT(pPSO && pSRB, "Depth PSO/SRB is null (check GetDepthPSO/GetDepthSRB).");

					if (pLastPSO != pPSO)
					{
						pLastPSO = pPSO;
						pLastSRB = nullptr;
						pContext->SetPipelineState(pLastPSO);
					}

					if (pLastSRB != pSRB)
					{
						pLastSRB = pSRB;
						pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					}

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

					DrawIndexedAttribs dia = pkt.DrawAttribs;

					// DRAW_CONSTANTS update
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

				// -------------------------
				// Indirect packets
				// -------------------------
				for (const DrawIndirectPacket& pkt : ctx.DepthPrepassIndirectDrawPackets)
				{
					ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

					IPipelineState* pPSO = pkt.PSO;
					IShaderResourceBinding* pSRB = pkt.SRB;

					ASSERT(pPSO && pSRB, "Depth PSO/SRB is null (check GetDepthPSO/GetDepthSRB).");

					if (pLastPSO != pPSO)
					{
						pLastPSO = pPSO;
						pLastSRB = nullptr;
						pContext->SetPipelineState(pLastPSO);
					}

					if (pLastSRB != pSRB)
					{
						pLastSRB = pSRB;
						pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					}

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

					DrawIndexedIndirectAttribs dia = pkt.DrawAttribs;

					// DRAW_CONSTANTS update
					{
						MapHelper<hlsl::DrawConstants> map(
							pContext,
							ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")),
							MAP_WRITE,
							MAP_FLAG_DISCARD);

						hlsl::DrawConstants* dst = map;
						dst->StartInstanceLocation = pkt.StartInstanceLocation;
					}

					pContext->DrawIndexedIndirect(dia);
				}
			});
	}
} // namespace shz
