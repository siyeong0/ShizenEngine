#include "pch.h"
#include "Engine/RenderSystem/Public/PostProcessSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	// ---------------------------------------------------------------------
	// IDs
	// ---------------------------------------------------------------------
	static const uint64 kLighting = STRING_HASH("LightingScene");
	static const uint64 kVelocity = STRING_HASH("Velocity");
	static const uint64 kDepth = STRING_HASH("GBufferDepth");
	static const uint64 kGBuffer2 = STRING_HASH("GBuffer2_MRAO");

	static const uint64 kHistory0 = STRING_HASH("TAA_History0");
	static const uint64 kHistory1 = STRING_HASH("TAA_History1");

	static inline bool IsEvenFrame(const RenderPassContext& ctx)
	{
		return (ctx.FrameIndex & 1u) == 0u;
	}

	void PostProcessSystem::Initialize(Renderer& renderer)
	{
		// TAA History Ping-Pong
		{
			TextureDesc td = {};
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.Format = TEX_FORMAT_RGBA16_FLOAT;
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

			td.Name = "TAA_History0";
			renderer.AddTexture(STRING_HASH("TAA_History0"), td);

			td.Name = "TAA_History1";
			renderer.AddTexture(STRING_HASH("TAA_History1"), td);
		}
	}

	// ---------------------------------------------------------------------
	// InstallPasses
	// ---------------------------------------------------------------------
	void PostProcessSystem::InstallPasses(Renderer& renderer)
	{
		// =============================================================
		// Pass: TAA_WriteHistory0
		//   Even frames only
		//   Reads: Lighting + History1 + (Velocity/Depth)
		//   Writes: History0 (fixed RTV)
		// =============================================================
		renderer.AddPass(
			"TAA_WriteHistory0",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(kLighting);
				// b.DeclareTextureSRVRead(kHistory1); // To avoid cycle

				// keep only if always created
				b.DeclareTextureSRVRead(kVelocity);
				b.DeclareTextureSRVRead(kDepth);
				b.DeclareTextureSRVRead(kGBuffer2);

				b.DeclareTextureRTVWrite(kHistory0);
			},
			[this](RenderPassContext& ctx)
			{
				if (!IsEvenFrame(ctx))
					return;

				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pTAAPSO_H0 && m_pTAASRB_H0, "TAA(H0) PSO/SRB is null.");

				IDeviceContext* devCtx = ctx.pImmediateContext;

				auto bindTex = [](IShaderResourceBinding* srb, const char* name, ITextureView* srv)
				{
					if (!srv) return;
					if (auto var = srb->GetVariableByName(SHADER_TYPE_PIXEL, name))
					{
						var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
				};

				// current = lighting, history(prev) = history1
				bindTex(m_pTAASRB_H0, "g_CurrentColor", ctx.pRegistry->GetTextureSRV(kLighting));
				bindTex(m_pTAASRB_H0, "g_HistoryColor", ctx.pRegistry->GetTextureSRV(kHistory1));
				bindTex(m_pTAASRB_H0, "g_Velocity", ctx.pRegistry->GetTextureSRV(kVelocity));
				bindTex(m_pTAASRB_H0, "g_Depth", ctx.pRegistry->GetTextureSRV(kDepth));
				bindTex(m_pTAASRB_H0, "g_GBuffer2", ctx.pRegistry->GetTextureSRV(kGBuffer2));

				devCtx->SetPipelineState(m_pTAAPSO_H0);
				devCtx->CommitShaderResources(m_pTAASRB_H0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				devCtx->Draw(da);
			},
				[this](RenderPassContext& ctx)
			{
				StateTransitionDesc barriers[] = { {ctx.pRenderer->GetTexture(kHistory1), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE} };
				ctx.pImmediateContext->TransitionResourceStates(_countof(barriers), barriers);
			},
				[this](RenderPassContext& ctx)
			{
				StateTransitionDesc barriers[] = 
				{
					{ctx.pRenderer->GetTexture(kHistory0), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE},
				};
				ctx.pImmediateContext->TransitionResourceStates(_countof(barriers), barriers);
			},
				[this, &renderer]()
			{
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc.Name = "TAA PSO (Write History0)";
				psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
				gp.RasterizerDesc.FrontCounterClockwise = true;
				gp.DepthStencilDesc.DepthEnable = false;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "TAA VS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.Desc.UseCombinedTextureSamplers = false;
				vsCI.FilePath = m_VS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "TAA PS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.Desc.UseCombinedTextureSamplers = false;
				psCI.FilePath = m_TAAPS.c_str();

				renderer.CreateShader(vsCI, &psoCi.pVS);
				renderer.CreateShader(psCI, &psoCi.pPS);
				ASSERT(psoCi.pVS && psoCi.pPS, "TAA shader compile failed.");

				psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_CurrentColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_HistoryColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_Velocity",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_Depth",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_GBuffer2",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				psoCi.PSODesc.ResourceLayout.Variables = vars;
				psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				SamplerDesc pointClamp =
				{
					FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_PIXEL, "g_PointClampSampler", pointClamp },
				};
				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pTAAPSO_H0 = renderer.AcquirePipelineState(STRING_HASH("TAA_WriteHistory0"), psoCi, true);
				ASSERT(m_pTAAPSO_H0, "AcquirePipelineState(TAA_WriteHistory0) failed.");

				m_pTAAPSO_H0->CreateShaderResourceBinding(&m_pTAASRB_H0, true);
				ASSERT(m_pTAASRB_H0, "TAA(H0) SRB create failed.");
			});

		// =============================================================
		// Pass: TAA_WriteHistory1
		//   Odd frames only
		//   Reads: Lighting + History0 + (Velocity/Depth)
		//   Writes: History1 (fixed RTV)
		// =============================================================
		renderer.AddPass(
			"TAA_WriteHistory1",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(kLighting);
				// b.DeclareTextureSRVRead(kHistory0); // To avoid cycle

				b.DeclareTextureSRVRead(kVelocity);
				b.DeclareTextureSRVRead(kDepth);
				b.DeclareTextureSRVRead(kGBuffer2);

				b.DeclareTextureRTVWrite(kHistory1);
			},
			[this](RenderPassContext& ctx)
			{
				if (IsEvenFrame(ctx))
					return;

				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pTAAPSO_H1 && m_pTAASRB_H1, "TAA(H1) PSO/SRB is null.");

				IDeviceContext* devCtx = ctx.pImmediateContext;

				auto bindTex = [](IShaderResourceBinding* srb, const char* name, ITextureView* srv)
				{
					if (!srv) return;
					if (auto var = srb->GetVariableByName(SHADER_TYPE_PIXEL, name))
					{
						var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
				};

				// current = lighting, history(prev) = history0
				bindTex(m_pTAASRB_H1, "g_CurrentColor", ctx.pRegistry->GetTextureSRV(kLighting));
				bindTex(m_pTAASRB_H1, "g_HistoryColor", ctx.pRegistry->GetTextureSRV(kHistory0));
				bindTex(m_pTAASRB_H1, "g_Velocity", ctx.pRegistry->GetTextureSRV(kVelocity));
				bindTex(m_pTAASRB_H1, "g_Depth", ctx.pRegistry->GetTextureSRV(kDepth));
				bindTex(m_pTAASRB_H1, "g_GBuffer2", ctx.pRegistry->GetTextureSRV(kGBuffer2));

				devCtx->SetPipelineState(m_pTAAPSO_H1);
				devCtx->CommitShaderResources(m_pTAASRB_H1, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				devCtx->Draw(da);
			},
				[this](RenderPassContext& ctx)
			{
				StateTransitionDesc barriers[] = 
				{ 
					{ctx.pRenderer->GetTexture(kHistory0), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
				};
				ctx.pImmediateContext->TransitionResourceStates(_countof(barriers), barriers);
			},
				[this](RenderPassContext& ctx)
			{
				StateTransitionDesc barriers[] =
				{
					{ctx.pRenderer->GetTexture(kHistory0), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE},
					{ctx.pRenderer->GetTexture(kHistory1), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE},
				};
				ctx.pImmediateContext->TransitionResourceStates(_countof(barriers), barriers);
			},
				[this, &renderer]()
			{
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc.Name = "TAA PSO (Write History1)";
				psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
				gp.RasterizerDesc.FrontCounterClockwise = true;
				gp.DepthStencilDesc.DepthEnable = false;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "TAA VS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.Desc.UseCombinedTextureSamplers = false;
				vsCI.FilePath = m_VS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "TAA PS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.Desc.UseCombinedTextureSamplers = false;
				psCI.FilePath = m_TAAPS.c_str();

				renderer.CreateShader(vsCI, &psoCi.pVS);
				renderer.CreateShader(psCI, &psoCi.pPS);
				ASSERT(psoCi.pVS && psoCi.pPS, "TAA shader compile failed.");

				psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_CurrentColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_HistoryColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_Velocity",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_Depth",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_GBuffer2",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				psoCi.PSODesc.ResourceLayout.Variables = vars;
				psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				SamplerDesc pointClamp =
				{
					FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_PIXEL, "g_PointClampSampler", pointClamp },
				};
				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pTAAPSO_H1 = renderer.AcquirePipelineState(STRING_HASH("TAA_WriteHistory1"), psoCi, true);
				ASSERT(m_pTAAPSO_H1, "AcquirePipelineState(TAA_WriteHistory1) failed.");

				m_pTAAPSO_H1->CreateShaderResourceBinding(&m_pTAASRB_H1, true);
				ASSERT(m_pTAASRB_H1, "TAA(H1) SRB create failed.");
			});

		// =============================================================
		// Pass: Post
		//   Reads: History0 + History1 (declare both for graph stability)
		//   Writes: SwapChain
		//   Runtime: bind one SRV depending on frame parity
		// =============================================================
		renderer.AddPass(
			"Post",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				// Declare both (graph sees stable deps)
				b.DeclareTextureSRVRead(kHistory0);
				b.DeclareTextureSRVRead(kHistory1);

				b.DeclareSwapChainRTVWrite();
				b.SetClearColor(STRING_HASH("SwapChain.BackBuffer"), 0.f, 0.f, 0.f, 1.f);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pSwapChain, "SwapChain is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pPostPSO && m_pPostSRB, "Post PSO/SRB is null.");

				IDeviceContext* devCtx = ctx.pImmediateContext;

				// Viewport: swapchain size
				{
					const SwapChainDesc& scDesc = ctx.pSwapChain->GetDesc();

					Viewport bbVp = {};
					bbVp.TopLeftX = 0;
					bbVp.TopLeftY = 0;
					bbVp.Width = float(scDesc.Width);
					bbVp.Height = float(scDesc.Height);
					bbVp.MinDepth = 0.f;
					bbVp.MaxDepth = 1.f;

					devCtx->SetViewports(1, &bbVp, 0, 0);
				}

				// IMPORTANT:
				// Even frame -> TAA wrote History0 this frame
				// Odd  frame -> TAA wrote History1 this frame
				const uint64 src = IsEvenFrame(ctx) ? kHistory0 : kHistory1;

				if (auto* v = m_pPostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor"))
				{
					v->Set(ctx.pRegistry->GetTextureSRV(src), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}

				devCtx->SetPipelineState(m_pPostPSO);
				devCtx->CommitShaderResources(m_pPostSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				devCtx->Draw(da);
			},
				[this, &renderer]()
			{
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc.Name = "Post Tonemap PSO";
				psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
				gp.RasterizerDesc.FrontCounterClockwise = true;
				gp.DepthStencilDesc.DepthEnable = false;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "Post VS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.Desc.UseCombinedTextureSamplers = false;
				vsCI.FilePath = m_VS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "Post PS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.Desc.UseCombinedTextureSamplers = false;
				psCI.FilePath = m_PostPS.c_str();

				renderer.CreateShader(vsCI, &psoCi.pVS);
				renderer.CreateShader(psCI, &psoCi.pPS);
				ASSERT(psoCi.pVS && psoCi.pPS, "Post shader compile failed.");

				psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_InputColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				psoCi.PSODesc.ResourceLayout.Variables = vars;
				psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
				};
				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pPostPSO = renderer.AcquirePipelineState(STRING_HASH("Post"), psoCi, true);
				ASSERT(m_pPostPSO, "AcquirePipelineState(Post) failed.");

				m_pPostPSO->CreateShaderResourceBinding(&m_pPostSRB, true);
				ASSERT(m_pPostSRB, "Post SRB create failed.");
			});
	}
} // namespace shz
