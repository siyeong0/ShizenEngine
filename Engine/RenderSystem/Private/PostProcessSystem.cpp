#include "pch.h"
#include "Engine/RenderSystem/Public/PostProcessSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	void PostProcessSystem::InstallPasses(Renderer& renderer)
	{
		renderer.AddPass(
			"Post",
			[](RenderPassBuilder& b)
			{
				const uint64 kLighting = STRING_HASH("LightingFinal");

				// Read final lighting result
				b.DeclareTextureSRVRead(kLighting);

				// Write to swapchain backbuffer RTV (external)
				b.DeclareSwapChainRTVWrite();

				// Clear BB (old code: 0,0,0,1)
				b.SetClearColor(STRING_HASH("SwapChain.BackBuffer"), 0.f, 0.f, 0.f, 1.f);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pSwapChain, "SwapChain is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pPostPSO, "Post PSO is null.");
				ASSERT(m_pPostSRB, "Post SRB is null.");

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

				// Bind SRV (Lighting -> PostCopy)
				{
					if (auto* v = m_pPostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor"))
					{
						v->Set(
							ctx.pRegistry->GetTextureSRV(STRING_HASH("LightingFinal")),
							SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
				}

				devCtx->SetPipelineState(m_pPostPSO);
				devCtx->CommitShaderResources(m_pPostSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				// Fullscreen triangle
				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				devCtx->Draw(da);
			},
				[this, &renderer]()
			{
				// Create PSO/SRB once. RenderPass is already created by Renderer(AddPass).
				// This must use AcquirePipelineState(passId, ...) so gp.pRenderPass gets wired.
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc.Name = "Post Copy PSO";
				psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
				gp.RasterizerDesc.FrontCounterClockwise = true;
				gp.DepthStencilDesc.DepthEnable = false;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "PostCopy VS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.Desc.UseCombinedTextureSamplers = false;
				vsCI.FilePath = m_VS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "PostCopy PS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.Desc.UseCombinedTextureSamplers = false;
				psCI.FilePath = m_PS.c_str();

				renderer.CreateShader(vsCI, &psoCi.pVS);
				renderer.CreateShader(psCI, &psoCi.pPS);
				ASSERT(psoCi.pVS && psoCi.pPS, "PostCopy shader compile failed.");

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

				// IMPORTANT: bind this PSO to the pass's renderpass
				m_pPostPSO = renderer.AcquirePipelineState(STRING_HASH("Post"), psoCi, true);
				ASSERT(m_pPostPSO, "AcquirePipelineState(Post) failed.");

				m_pPostPSO->CreateShaderResourceBinding(&m_pPostSRB, true);
				ASSERT(m_pPostSRB, "Post SRB create failed.");
			});
	}
} // namespace shz
