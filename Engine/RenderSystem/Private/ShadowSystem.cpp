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
						dst->StartInstanceLocation = dia.FirstInstanceLocation;
					}

					pContext->DrawIndexed(dia);
				}
			},
				[this, &renderer]()
			{
				// -----------------------------------------------------------------
				// Opaque shadow PSO
				// -----------------------------------------------------------------
				{
					GraphicsPipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "Shadow PSO";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

					GraphicsPipelineDesc& gp = psoCI.GraphicsPipeline;
					gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

					// Depth-only pass
					gp.NumRenderTargets = 0;
					gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
					gp.DSVFormat = TEX_FORMAT_UNKNOWN;

					gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
					gp.RasterizerDesc.FrontCounterClockwise = true;

					gp.DepthStencilDesc.DepthEnable = true;
					gp.DepthStencilDesc.DepthWriteEnable = true;
					gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

					LayoutElement layoutElems[] =
					{
						LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 Position (vertex stream)
					};
					layoutElems[0].Stride = sizeof(float) * 11;
					gp.InputLayout.LayoutElements = layoutElems;
					gp.InputLayout.NumElements = _countof(layoutElems);

					ShaderCreateInfo vsCI = {};
					vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					vsCI.EntryPoint = "main";
					vsCI.Desc.Name = "Shadow VS";
					vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
					vsCI.Desc.UseCombinedTextureSamplers = false;
					vsCI.FilePath = m_ShadowVS.c_str();

					ShaderCreateInfo psCI = {};
					psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					psCI.EntryPoint = "main";
					psCI.Desc.Name = "Shadow PS";
					psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
					psCI.Desc.UseCombinedTextureSamplers = false;
					psCI.FilePath = m_ShadowPS.c_str();

					renderer.CreateShader(vsCI, &psoCI.pVS);
					renderer.CreateShader(psCI, &psoCI.pPS);
					ASSERT(psoCI.pVS && psoCI.pPS, "Shadow shaders compile failed.");

					// Resource layout:
					// - Opaque shadow usually needs per-frame/per-draw buffers, which you already bind commonly.
					// - If Shadow PS samples anything, declare vars here; otherwise keep minimal.
					psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					// Acquire by passId (sets gp.pRenderPass + subpass etc)
					m_pShadowOpaquePSO = renderer.AcquirePipelineState(STRING_HASH("Shadow"), psoCI, true);
					ASSERT(m_pShadowOpaquePSO, "AcquirePipelineState(Shadow) failed.");
				}

				// -----------------------------------------------------------------
				// Masked shadow PSO
				// - alpha cutout needs basecolor/opacity texture sampling, so we make
				//   a separate PSO (as your old code intended).
				// -----------------------------------------------------------------
				{
					GraphicsPipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "ShadowMasked PSO";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

					GraphicsPipelineDesc& gp = psoCI.GraphicsPipeline;
					gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

					gp.NumRenderTargets = 0;
					gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
					gp.DSVFormat = TEX_FORMAT_UNKNOWN;

					gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
					gp.RasterizerDesc.FrontCounterClockwise = true;

					gp.DepthStencilDesc.DepthEnable = true;
					gp.DepthStencilDesc.DepthWriteEnable = true;
					gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

					LayoutElement layoutElems[] =
					{
						LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
						LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
					};
					layoutElems[0].Stride = sizeof(float) * 11;
					layoutElems[1].Stride = sizeof(float) * 11;
					gp.InputLayout.LayoutElements = layoutElems;
					gp.InputLayout.NumElements = _countof(layoutElems);

					ShaderCreateInfo vsCI = {};
					vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					vsCI.EntryPoint = "main";
					vsCI.Desc.Name = "ShadowMasked VS";
					vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
					vsCI.Desc.UseCombinedTextureSamplers = false;
					vsCI.FilePath = m_ShadowMaskedVS.c_str();

					ShaderCreateInfo psCI = {};
					psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					psCI.EntryPoint = "main";
					psCI.Desc.Name = "ShadowMasked PS";
					psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
					psCI.Desc.UseCombinedTextureSamplers = false;
					psCI.FilePath = m_ShadowMaskedPS.c_str();

					renderer.CreateShader(vsCI, &psoCI.pVS);
					renderer.CreateShader(psCI, &psoCI.pPS);
					ASSERT(psoCI.pVS && psoCI.pPS, "ShadowMasked shaders compile failed.");

					psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						{ SHADER_TYPE_PIXEL, "g_BaseColorTex",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					psoCI.PSODesc.ResourceLayout.Variables = vars;
					psoCI.PSODesc.ResourceLayout.NumVariables = _countof(vars);

					SamplerDesc linearWrap =
					{
						FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
						TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
					};

					ImmutableSamplerDesc samplers[] =
					{
						{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrap },
					};
					psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
					psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

					m_pShadowMaskedPSO = renderer.AcquirePipelineState(STRING_HASH("Shadow"), psoCI, true);
					ASSERT(m_pShadowMaskedPSO, "AcquirePipelineState(ShadowMasked) failed.");
				}

				renderer.SetShadowPipeline(m_pShadowOpaquePSO, m_pShadowMaskedPSO);
			});
	}
} // namespace shz
