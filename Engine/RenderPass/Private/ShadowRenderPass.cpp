// Engine/RenderPass/Private/ShadowRenderPass.cpp
#include "pch.h"
#include "Engine/RenderPass/Public/ShadowRenderPass.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"

#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"
#include "Engine/Renderer/Public/RendererMaterialStaticBinder.h"

namespace shz
{
	namespace
	{
		static constexpr uint32 SHADOW_MAP_SIZE = 1024 * 16; // TODO: Runtime setting?
	}

	bool ShadowRenderPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(ctx.pShaderSourceFactory, "Shader source factory is null.");
		ASSERT(ctx.pShadowCB, "ShadowCB is null.");
		ASSERT(ctx.pObjectTableSB, "ObjectTableSB is null.");
		ASSERT(ctx.pObjectIndexVB, "ObjectIndexVB is null.");

		m_Width = SHADOW_MAP_SIZE;
		m_Height = SHADOW_MAP_SIZE;

		// ------------------------------------------------------------
		// Create shadow map texture + views
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "ShadowMap";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = m_Width;
			td.Height = m_Height;
			td.MipLevels = 1;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.Format = TEX_FORMAT_R32_TYPELESS;
			td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

			m_pShadowMap.Release();
			m_pShadowDSV = nullptr;
			m_pShadowSRV = nullptr;

			ctx.pDevice->CreateTexture(td, nullptr, &m_pShadowMap);
			ASSERT(m_pShadowMap, "Failed to create shadow map texture.");

			{
				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
				vd.Format = TEX_FORMAT_D32_FLOAT;
				m_pShadowMap->CreateView(vd, &m_pShadowDSV);
			}

			{
				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
				vd.Format = TEX_FORMAT_R32_FLOAT;
				m_pShadowMap->CreateView(vd, &m_pShadowSRV);
			}

			ASSERT(m_pShadowDSV, "Shadow DSV is null.");
			ASSERT(m_pShadowSRV, "Shadow SRV is null.");
		}

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
				ITextureView* atch[1] = { m_pShadowDSV };

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
		// Create Opaque Shadow PSO + SRB (same as old createShadowPso)
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
			gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
			gp.RasterizerDesc.FrontCounterClockwise = true;

			gp.DepthStencilDesc.DepthEnable = true;
			gp.DepthStencilDesc.DepthWriteEnable = true;
			gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

			LayoutElement layoutElems[] =
			{
				LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 Position (vertex stream)
				LayoutElement{4, 1, 1, VT_UINT32,  false, LAYOUT_ELEMENT_AUTO_OFFSET, sizeof(uint32), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1}, // ATTRIB4 ObjectIndex
			};
			layoutElems[0].Stride = sizeof(float) * 11;
			layoutElems[1].Stride = sizeof(uint32);

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
				sci.FilePath = m_VS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;
				ctx.pDevice->CreateShader(sci, &vs);
				ASSERT(vs, "Failed to create Shadow VS.");
			}

			RefCntAutoPtr<IShader> ps;
			{
				sci.Desc = {};
				sci.Desc.Name = "Shadow PS";
				sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
				sci.FilePath = m_PS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;
				ctx.pDevice->CreateShader(sci, &ps);
				ASSERT(ps, "Failed to create Shadow PS.");
			}

			psoCi.pVS = vs;
			psoCi.pPS = ps;

			psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
			psoCi.PSODesc.ResourceLayout.Variables = nullptr;
			psoCi.PSODesc.ResourceLayout.NumVariables = 0;

			m_pShadowPSO.Release();
			ctx.pDevice->CreateGraphicsPipelineState(psoCi, &m_pShadowPSO);
			ASSERT(m_pShadowPSO, "Shadow PSO create failed.");

			// Bind statics (same as old)
			{
				if (auto* var = m_pShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS"))
				{
					var->Set(ctx.pShadowCB);
				}

				if (auto* var = m_pShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
				{
					var->Set(ctx.pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
				}
			}

			m_pSRB.Release();
			m_pShadowPSO->CreateShaderResourceBinding(&m_pSRB, true);
			ASSERT(m_pSRB, "Shadow SRB create failed.");
		}

		// ------------------------------------------------------------
		// Create Masked Shadow PSO (same as old createShadowMaskedPso)
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
				LayoutElement{4, 1, 1, VT_UINT32,  false, LAYOUT_ELEMENT_AUTO_OFFSET, sizeof(uint32), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1}, // ObjectIndex
			};
			layoutElems[0].Stride = sizeof(float) * 11;
			layoutElems[1].Stride = sizeof(float) * 11;
			layoutElems[2].Stride = sizeof(uint32);

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
				sci.FilePath = m_MaskedVS.c_str();
				sci.Desc.UseCombinedTextureSamplers = false;
				ctx.pDevice->CreateShader(sci, &vs);
				ASSERT(vs, "Failed to create ShadowMasked VS.");
			}

			RefCntAutoPtr<IShader> ps;
			{
				sci.Desc = {};
				sci.Desc.Name = "Shadow Masked PS";
				sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
				sci.FilePath = m_MaskedPS.c_str();
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

			m_pShadowMaskedPSO.Release();
			ctx.pDevice->CreateGraphicsPipelineState(psoCi, &m_pShadowMaskedPSO);
			ASSERT(m_pShadowMaskedPSO, "Shadow Masked PSO create failed.");

			// Bind statics (same as old)
			{
				if (auto* var = m_pShadowMaskedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS"))
				{
					var->Set(ctx.pShadowCB);
				}

				if (auto* var = m_pShadowMaskedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
				{
					var->Set(ctx.pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
				}
			}
		}

		return true;
	}

	void ShadowRenderPass::Cleanup()
	{
		m_pSRB.Release();

		m_pShadowPSO.Release();
		m_pShadowMaskedPSO.Release();

		m_pFramebuffer.Release();
		m_pRenderPass.Release();

		m_pShadowMap.Release();
		m_pShadowDSV = nullptr;
		m_pShadowSRV = nullptr;

		m_Width = 0;
		m_Height = 0;
	}

	void ShadowRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		(void)ctx;
		m_DrawCallCount = 0;
	}

	void ShadowRenderPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "Context is null.");

		IDeviceContext* pCtx = ctx.pImmediateContext;

		const std::vector<DrawPacket>& packets = ctx.GetPassPackets("Shadow");
		if (packets.empty())
		{
			return;
		}

		// 0) to DEPTH_WRITE (always)
		{
			StateTransitionDesc tr =
			{
				m_pShadowMap,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_DEPTH_WRITE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			pCtx->TransitionResourceStates(1, &tr);
		}

		Viewport vp = {};
		vp.Width = float(m_Width);
		vp.Height = float(m_Height);
		vp.MinDepth = 0.f;
		vp.MaxDepth = 1.f;
		pCtx->SetViewports(1, &vp, 0, 0);

		OptimizedClearValue clearVals[1] = {};
		clearVals[0].DepthStencil.Depth = 1.f;
		clearVals[0].DepthStencil.Stencil = 0;

		BeginRenderPassAttribs rp = {};
		rp.pRenderPass = m_pRenderPass;
		rp.pFramebuffer = m_pFramebuffer;
		rp.ClearValueCount = 1;
		rp.pClearValues = clearVals;

		pCtx->BeginRenderPass(rp);

		IPipelineState* pLastPSO = nullptr;
		IShaderResourceBinding* pLastSRB = nullptr;
		IBuffer* pLastVB = nullptr;
		IBuffer* pLastIB = nullptr;
		bool bBoundObjectIndexVB = false;

		for (const DrawPacket& pkt : packets)
		{
			ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

			// Bind PSO
			if (pLastPSO != pkt.PSO)
			{
				pLastPSO = pkt.PSO;
				pLastSRB = nullptr;
				pCtx->SetPipelineState(pLastPSO);
			}

			IShaderResourceBinding* pSRB = pkt.SRB ? pkt.SRB : m_pSRB.RawPtr();
			ASSERT(pSRB, "SRB is null.");

			if (pLastSRB != pSRB)
			{
				pLastSRB = pSRB;

				pCtx->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			}

			if (pLastVB != pkt.VertexBuffer || !bBoundObjectIndexVB)
			{
				IBuffer* ppVertexBuffers[] = { pkt.VertexBuffer, ctx.pObjectIndexVB };
				uint64 pOffsets[] = { 0, 0 };
				pCtx->SetVertexBuffers(
					0,
					2, 
					ppVertexBuffers, 
					pOffsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pLastVB = pkt.VertexBuffer;
				bBoundObjectIndexVB = true;
			}

			if (pLastIB != pkt.IndexBuffer)
			{
				pCtx->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
				pLastIB = pkt.IndexBuffer;
			}

			ctx.UploadObjectIndexInstance(pkt.ObjectIndex);

			DrawIndexedAttribs dia = pkt.DrawAttribs;
#ifdef SHZ_DEBUG
			if (dia.Flags == DRAW_FLAG_NONE) dia.Flags = DRAW_FLAG_VERIFY_ALL;
#endif

			pCtx->DrawIndexed(dia);
#ifdef PROFILING
			++m_DrawCallCount;
#endif
		}

		pCtx->EndRenderPass();

		{
			StateTransitionDesc tr2 =
			{
				m_pShadowMap,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			pCtx->TransitionResourceStates(1, &tr2);
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
