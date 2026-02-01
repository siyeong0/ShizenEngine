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

	ShadowRenderPass::ShadowRenderPass()
	{

	}

	ShadowRenderPass::~ShadowRenderPass()
	{
		m_pFramebuffer.Release();
		m_pRenderPass.Release();

		m_pShadowSRB.Release();
		m_pShadowPSO.Release();
		m_pShadowMaskedPSO.Release();
	}

	void ShadowRenderPass::Initialize(RenderPassContext& ctx)
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

		// ------------------------------------------------------------
		// Create Opaque Shadow PSO + SRB
		// ------------------------------------------------------------
		{
			GraphicsPipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc.Name = "Shadow PSO";
			psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

			GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
			gp.pRenderPass = m_pRenderPass;
			gp.SubpassIndex = 0;

			gp.NumRenderTargets = 0;
			gp.DSVFormat = TEX_FORMAT_UNKNOWN;

			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			// gp.RasterizerDesc.CullMode = CULL_MODE_BACK; // TODO: Only terrain?
			gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
			gp.RasterizerDesc.FrontCounterClockwise = true;
			gp.RasterizerDesc.SlopeScaledDepthBias = 0.0f;
			gp.RasterizerDesc.DepthBias = 0;
			gp.RasterizerDesc.DepthBiasClamp = 0.0f;

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

			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;
			sci.EntryPoint = "main";
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

			RefCntAutoPtr<IShader> vs;
			{
				sci.Desc = {};
				sci.Desc.Name = "Shadow VS";
				sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
				sci.FilePath = m_ShadowVS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;
				ctx.pDevice->CreateShader(sci, &vs);
				ASSERT(vs, "Failed to create Shadow VS.");
			}

			RefCntAutoPtr<IShader> ps;
			{
				sci.Desc = {};
				sci.Desc.Name = "Shadow PS";
				sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
				sci.FilePath = m_ShadowPS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;
				ctx.pDevice->CreateShader(sci, &ps);
				ASSERT(ps, "Failed to create Shadow PS.");
			}

			psoCi.pVS = vs;
			psoCi.pPS = ps;

			psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
			psoCi.PSODesc.ResourceLayout.Variables = nullptr;
			psoCi.PSODesc.ResourceLayout.NumVariables = 0;

			m_pShadowPSO = ctx.pPipelineStateManager->AcquireGraphics(psoCi);
			ASSERT(m_pShadowPSO, "Shadow PSO create failed.");

			// Bind statics (same as old)
			{
				if (auto* var = m_pShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
				{
					var->Set(ctx.pRegistry->GetBufferSRV(STRING_HASH("ObjectTable.Shadow")));
				}
			}

			ASSERT(!m_pShadowSRB, "Shadow SRB already exists.");
			m_pShadowPSO->CreateShaderResourceBinding(&m_pShadowSRB, true);
			ASSERT(m_pShadowSRB, "Shadow SRB create failed.");
		}

		// ------------------------------------------------------------
		// Create Masked Shadow PSO
		// ------------------------------------------------------------
		{
			GraphicsPipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc.Name = "Shadow Masked PSO";
			psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

			GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
			gp.pRenderPass = m_pRenderPass;
			gp.SubpassIndex = 0;

			gp.NumRenderTargets = 0;
			gp.DSVFormat = TEX_FORMAT_UNKNOWN;

			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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

			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;
			sci.EntryPoint = "main";
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

			RefCntAutoPtr<IShader> vs;
			{
				sci.Desc = {};
				sci.Desc.Name = "Shadow Masked VS";
				sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
				sci.FilePath = m_ShadowMaskedVS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;
				ctx.pDevice->CreateShader(sci, &vs);
				ASSERT(vs, "Failed to create ShadowMasked VS.");
			}

			RefCntAutoPtr<IShader> ps;
			{
				sci.Desc = {};
				sci.Desc.Name = "Shadow Masked PS";
				sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
				sci.FilePath = m_ShadowMaskedPS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;
				ctx.pDevice->CreateShader(sci, &ps);
				ASSERT(ps, "Failed to create ShadowMasked PS.");
			}

			psoCi.pVS = vs;
			psoCi.pPS = ps;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_PIXEL, "g_BaseColorTex",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_PIXEL, "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			};
			psoCi.PSODesc.ResourceLayout.Variables = vars;
			psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

			SamplerDesc linearWrap =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			ImmutableSamplerDesc samplers[] =
			{
				{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrap }
			};
			psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
			psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

			m_pShadowMaskedPSO = ctx.pPipelineStateManager->AcquireGraphics(psoCi);
			ASSERT(m_pShadowMaskedPSO, "Shadow Masked PSO create failed.");

			// Bind statics (same as old)
			{
				if (auto* var = m_pShadowMaskedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
				{
					var->Set(ctx.pRegistry->GetBufferSRV(STRING_HASH("ObjectTable.Shadow")));
				}
			}
		}
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
