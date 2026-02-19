#include "pch.h"
#include "Engine/RenderSystem/Public/GBufferSystem.h"

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

	void GBufferSystem::Initialize(Renderer& renderer)
	{
		uint32 width = renderer.GetWidth();
		uint32 height = renderer.GetHeight();

		// GBuffer textures
		static constexpr uint32 NUM_GBUFFERS = 4;
		{
			auto createGBufferTextureDesc = [&](uint32 w, uint32 h, TEXTURE_FORMAT fmt, const char* name) -> TextureDesc
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
				return td;
			};
			renderer.AddTexture(STRING_HASH("GBuffer0_Albedo"), createGBufferTextureDesc(width, height, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_Albedo"));
			renderer.AddTexture(STRING_HASH("GBuffer1_Normal"), createGBufferTextureDesc(width, height, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_Normal"));
			renderer.AddTexture(STRING_HASH("GBuffer2_MRAO"), createGBufferTextureDesc(width, height, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO"));
			renderer.AddTexture(STRING_HASH("GBuffer3_Emissive"), createGBufferTextureDesc(width, height, TEX_FORMAT_RGBA8_UNORM, "GBuffer3_Emissive"));

			// Velocity
			{
				TextureDesc td = {};
				td.Name = "Velocity";
				td.Type = RESOURCE_DIM_TEX_2D;
				td.Width = width;
				td.Height = height;
				td.MipLevels = 1;
				td.Format = TEX_FORMAT_RG16_FLOAT;
				td.SampleCount = 1;
				td.Usage = USAGE_DEFAULT;
				td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

				renderer.AddTexture(STRING_HASH("Velocity"), td);
			}

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

				renderer.AddTexture(STRING_HASH("GBufferDepth"), td);

				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
				vd.Format = TEX_FORMAT_D32_FLOAT;

				renderer.AddTextureView(STRING_HASH("GBufferDepth"), vd);

				vd = {};
				vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
				vd.Format = TEX_FORMAT_R32_FLOAT;
				renderer.AddTextureView(STRING_HASH("GBufferDepth"), vd);
			}
		}
	}

	void GBufferSystem::InstallPasses(Renderer& renderer)
	{
		// ------------------------------------------------------------
		// Pass: GBuffer
		// ------------------------------------------------------------
		renderer.AddPass(
			"GBuffer",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				const uint64 kAlbedo = STRING_HASH("GBuffer0_Albedo");
				const uint64 kNormal = STRING_HASH("GBuffer1_Normal");
				const uint64 kMRAO = STRING_HASH("GBuffer2_MRAO");
				const uint64 kEmissive = STRING_HASH("GBuffer3_Emissive");
				const uint64 kVelocity = STRING_HASH("Velocity");
				const uint64 kDepth = STRING_HASH("GBufferDepth");

				b.DeclareTextureRTVWrite(kAlbedo);
				b.DeclareTextureRTVWrite(kNormal);
				b.DeclareTextureRTVWrite(kMRAO);
				b.DeclareTextureRTVWrite(kEmissive);
				b.DeclareTextureRTVWrite(kVelocity);
				b.DeclareTextureDSVWrite(kDepth);

				b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectArgsBuffer"));
				b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectDrawCountBuffer"));

				b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD0"));
				b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD1"));
				b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD2"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesLodOffsets"));

				b.SetClearColor(kAlbedo, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kNormal, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kMRAO, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kEmissive, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kVelocity, 0.f, 0.f, 0.f, 0.f);
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

				for (const DrawPacket& pkt : ctx.MainDrawPackets)
				{
					ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

					if (pLastPSO != pkt.PSO)
					{
						pLastPSO = pkt.PSO;
						pLastSRB = nullptr;
						pContext->SetPipelineState(pLastPSO);
					}

					if (pLastSRB != pkt.SRB)
					{
						pLastSRB = pkt.SRB;
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

				for (const DrawIndirectPacket& pkt : ctx.MainIndirectPackets)
				{
					ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

					if (pLastPSO != pkt.PSO)
					{
						pLastPSO = pkt.PSO;
						pLastSRB = nullptr;
						pContext->SetPipelineState(pLastPSO);
					}

					if (pLastSRB != pkt.SRB)
					{
						pLastSRB = pkt.SRB;
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
