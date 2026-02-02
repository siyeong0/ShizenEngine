#include "pch.h"
#include "Engine/RenderPass/Public/GrassRenderPass.h"

#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	GrassRenderPass::GrassRenderPass()
	{
	}

	GrassRenderPass::~GrassRenderPass()
	{
		m_pFramebuffer.Release();
		m_pRenderPass.Release();

		m_pGrassSRB.Release();
		m_pGrassPSO.Release();

		m_pGrassMesh = nullptr;
	}

	void GrassRenderPass::Initialize(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");

		// ------------------------------------------------------------
		// RenderGraph declarations (Renderer auto-orders + auto-transitions)
		// ------------------------------------------------------------
		{
			// Lighting은 LOAD 기반 누적 렌더링 => readwrite 선언!
			DeclareTextureRTVReadWrite(STRING_HASH("Lighting"));

			// Depth도 LOAD 기반 => readwrite 선언!
			DeclareTextureDSVRead(STRING_HASH("GBufferDepth"));

			// Inputs
			DeclareTextureSRVRead(STRING_HASH("ShadowMap"));

			// Instance data + indirect args (ordering용)
			DeclareBufferSRVRead(STRING_HASH("GrassInstanceBuffer"));
			DeclareBufferIndirectArgsRead(STRING_HASH("GrassIndirectArgs"));

			// Constants
			DeclareBufferCBVRead(STRING_HASH("GrassRenderConstantsCB"));
			DeclareBufferCBVRead(STRING_HASH("DRAW_CONSTANTS"));
		}

		// ------------------------------------------------------------
		// Create RenderPass (Color=LOAD, Depth=LOAD)
		// - 실제 출력은 Lighting + GBufferDepth
		// ------------------------------------------------------------
		{
			const SwapChainDesc& scDesc = ctx.pSwapChain->GetDesc();
			const TEXTURE_FORMAT colorFmt = scDesc.ColorBufferFormat;

			const TEXTURE_FORMAT depthFmt = ctx.pRegistry->GetTextureDSV(STRING_HASH("GBufferDepth"))->GetDesc().Format;
			ASSERT(depthFmt != TEX_FORMAT_UNKNOWN, "Depth DSV format is unknown.");

			RenderPassAttachmentDesc atts[2] = {};

			atts[0].Format = colorFmt;
			atts[0].SampleCount = 1;
			atts[0].LoadOp = ATTACHMENT_LOAD_OP_LOAD;
			atts[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
			atts[0].StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
			atts[0].StencilStoreOp = ATTACHMENT_STORE_OP_STORE;
			atts[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
			atts[0].FinalState = RESOURCE_STATE_RENDER_TARGET;

			atts[1].Format = depthFmt;
			atts[1].SampleCount = 1;
			atts[1].LoadOp = ATTACHMENT_LOAD_OP_LOAD;
			atts[1].StoreOp = ATTACHMENT_STORE_OP_STORE;
			atts[1].StencilLoadOp = ATTACHMENT_LOAD_OP_LOAD;
			atts[1].StencilStoreOp = ATTACHMENT_STORE_OP_STORE;
			atts[1].InitialState = RESOURCE_STATE_DEPTH_WRITE;
			atts[1].FinalState = RESOURCE_STATE_DEPTH_WRITE;

			AttachmentReference colorRef = {};
			colorRef.AttachmentIndex = 0;
			colorRef.State = RESOURCE_STATE_RENDER_TARGET;

			AttachmentReference depthRef = {};
			depthRef.AttachmentIndex = 1;
			depthRef.State = RESOURCE_STATE_DEPTH_WRITE;

			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = 1;
			subpass.pRenderTargetAttachments = &colorRef;
			subpass.pDepthStencilAttachment = &depthRef;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = "RP_Grass";
			rpDesc.AttachmentCount = 2;
			rpDesc.pAttachments = atts;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			ctx.pDevice->CreateRenderPass(rpDesc, &m_pRenderPass);
			ASSERT(m_pRenderPass, "Failed to create Grass RenderPass.");
		}

		// ------------------------------------------------------------
		// Graphics PSO: Grass
		// ------------------------------------------------------------
		{
			ShaderCreateInfo vsCI = {};
			vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			vsCI.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;
			vsCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			vsCI.Desc.Name = "GrassVS";
			vsCI.EntryPoint = "main";
			vsCI.FilePath = "GrassForward.vsh";

			ShaderCreateInfo psCI = {};
			psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			psCI.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;
			psCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			psCI.Desc.Name = "GrassPS";
			psCI.EntryPoint = "main";
			psCI.FilePath = "GrassForward.psh";

			RefCntAutoPtr<IShader> vs, ps;
			ctx.pDevice->CreateShader(vsCI, &vs);
			ctx.pDevice->CreateShader(psCI, &ps);
			ASSERT(vs && ps, "CreateShader(GrassVS/PS) failed.");

			GraphicsPipelineStateCreateInfo psoCI = {};
			psoCI.PSODesc.Name = "PSO_Grass";
			psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

			auto& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_PIXEL, "g_BaseColorTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			};
			rl.Variables = vars;
			rl.NumVariables = _countof(vars);

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
			rl.ImmutableSamplers = samplers;
			rl.NumImmutableSamplers = _countof(samplers);

			auto& gp = psoCI.GraphicsPipeline;
			gp.pRenderPass = m_pRenderPass;
			gp.SubpassIndex = 0;
			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			gp.NumRenderTargets = 0;
			gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
			gp.DSVFormat = TEX_FORMAT_UNKNOWN;

			gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
			gp.RasterizerDesc.FrontCounterClockwise = true;

			gp.DepthStencilDesc.DepthEnable = true;
			gp.DepthStencilDesc.DepthWriteEnable = true;
			gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

			psoCI.pVS = vs;
			psoCI.pPS = ps;

			static LayoutElement layoutElems[] =
			{
				LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
				LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
				LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
				LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
			};
			gp.InputLayout.LayoutElements = layoutElems;
			gp.InputLayout.NumElements = _countof(layoutElems);

			m_pGrassPSO = ctx.pPipelineStateManager->AcquireGraphics(psoCI);
			ASSERT(m_pGrassPSO, "AcquireGraphics(PSO_Grass) failed.");

			if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances"))
			{
				var->Set(ctx.pRegistry->GetBufferSRV(STRING_HASH("GrassInstanceBuffer")));
			}

			if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
			}
			if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "GRASS_RENDER_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
			}

			m_pGrassPSO->CreateShaderResourceBinding(&m_pGrassSRB, true);
			ASSERT(m_pGrassSRB, "Create SRB for Grass failed.");
		}

		// ------------------------------------------------------------
		// Framebuffer
		// ------------------------------------------------------------
		buildFramebuffer(ctx);

		// ------------------------------------------------------------
		// Grass model
		// ------------------------------------------------------------
		{
			AssetRef<StaticMesh> grassRef = ctx.pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/GrassBlade.shzmesh.json");
			AssetPtr<StaticMesh> grassPtr = ctx.pAssetManager->LoadBlocking<StaticMesh>(grassRef);
			ASSERT(grassPtr && grassPtr->IsValid(), "Failed to load grass mesh.");

			grassPtr->RecomputeBounds();
			const Box& b = grassPtr->GetBounds();
			float yScale01 = 1.0f / (b.Max.y - b.Min.y);
			grassPtr->ApplyUniformScale(yScale01);
			grassPtr->MoveBottomToOrigin(true);

			m_pGrassMesh = &ctx.pRenderer->CreateStaticMeshRenderData(*grassPtr);
			ASSERT(m_pGrassMesh, "CreateStaticMeshRenderData failed.");

			StateTransitionDesc barriers[] =
			{
				{m_pGrassMesh->VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
				{m_pGrassMesh->IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			};

			ctx.pImmediateContext->TransitionResourceStates(_countof(barriers), barriers);
		}
	}

	void GrassRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		buildFramebuffer(ctx);
	}

	void GrassRenderPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(m_pRenderPass, "Grass RenderPass is null.");
		ASSERT(m_pFramebuffer, "Grass Framebuffer is null.");
		ASSERT(m_pGrassMesh, "GrassMesh is null.");

		IDeviceContext* pContext = ctx.pImmediateContext;

		// Bind per-frame texture inputs (optional)
		// NOTE: g_BaseColorTex 바인딩은 너 프로젝트의 텍스처 경로/ID에 맞춰서 연결하면 됨.
		// if (auto* v = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
		// {
		// 	v->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("GrassBaseColor")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		// }

		// Begin render pass + DrawIndexedIndirect
		{
			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_pRenderPass;
			rp.pFramebuffer = m_pFramebuffer;
			rp.ClearValueCount = 0;
			rp.pClearValues = nullptr;

			pContext->BeginRenderPass(rp);

			pContext->SetPipelineState(m_pGrassPSO);
			pContext->CommitShaderResources(m_pGrassSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			// VB/IB (transition-safe)
			{
				ASSERT(m_pGrassMesh->VertexBuffer, "Grass mesh VB is null.");
				ASSERT(m_pGrassMesh->IndexBuffer, "Grass mesh IB is null.");

				IBuffer* ppVertexBuffers[] = { m_pGrassMesh->VertexBuffer };
				uint64 offsets[] = { 0 };

				pContext->SetVertexBuffers(
					0, 1, ppVertexBuffers, offsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pContext->SetIndexBuffer(
					m_pGrassMesh->IndexBuffer, 0,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			}

			// Per-draw: StartInstanceLocation = 0
			{
				MapHelper<hlsl::DrawConstants> map(
					pContext,
					ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")),
					MAP_WRITE,
					MAP_FLAG_DISCARD);

				map->StartInstanceLocation = 0;
			}

			DrawIndexedIndirectAttribs ia = {};
			ia.IndexType = m_pGrassMesh->IndexType;
			ia.pAttribsBuffer = ctx.pRegistry->GetBuffer(STRING_HASH("GrassIndirectArgs"));
			ia.DrawArgsOffset = 0;
			ia.DrawCount = 1;
			ia.DrawArgsStride = 20;

#ifdef SHZ_DEBUG
			ia.Flags = DRAW_FLAG_VERIFY_ALL;
#endif
			ia.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

			ia.pCounterBuffer = nullptr;
			ia.CounterOffset = 0;
			ia.CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

			pContext->DrawIndexedIndirect(ia);

			pContext->EndRenderPass();
		}
	}

	void GrassRenderPass::EndFrame(RenderPassContext& ctx)
	{
		(void)ctx;
	}

	void GrassRenderPass::ReleaseSwapChainBuffers(RenderPassContext& ctx)
	{
		(void)ctx;
		m_pFramebuffer.Release();
	}

	void GrassRenderPass::OnResize(RenderPassContext& ctx, uint32 width, uint32 height)
	{
		(void)ctx;
		(void)width;
		(void)height;

		m_pFramebuffer.Release();
	}

	bool GrassRenderPass::buildFramebuffer(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(m_pRenderPass, "Grass render pass is null.");

		ITextureView* pRTV = ctx.pRegistry->GetTextureRTV(STRING_HASH("Lighting"));
		ITextureView* pDSV = ctx.pRegistry->GetTextureDSV(STRING_HASH("GBufferDepth"));
		ASSERT(pRTV, "Lighting RTV is null.");
		ASSERT(pDSV, "Depth DSV is null.");

		FramebufferDesc fbDesc = {};
		fbDesc.Name = "FB_Grass";
		fbDesc.pRenderPass = m_pRenderPass;

		ITextureView* attachments[2] = {};
		attachments[0] = pRTV;
		attachments[1] = pDSV;

		fbDesc.AttachmentCount = 2;
		fbDesc.ppAttachments = attachments;

		m_pFramebuffer.Release();
		ctx.pDevice->CreateFramebuffer(fbDesc, &m_pFramebuffer);
		ASSERT(m_pFramebuffer, "Failed to create Grass Framebuffer.");

		return true;
	}
} // namespace shz
