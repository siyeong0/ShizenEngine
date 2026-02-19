#include "pch.h"
#include "Engine/RenderSystem/Public/LightingSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	void LightingSystem::Initialize(Renderer& renderer)
	{
		{
			TextureDesc td = {};
			td.Name = "LightingScene";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;
			td.Format = renderer.GetSwapChainFormat();
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("LightingScene"), td);
		}
	}

	void LightingSystem::InstallPasses(Renderer& renderer)
	{
		// ------------------------------------------------------------
		// Pass: Lighting
		// ------------------------------------------------------------
		renderer.AddPass(
			"LightingScene",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer0_Albedo"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer2_MRAO"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer3_Emissive"));
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));

				// Shadow map array (existing)
				b.DeclareTextureSRVRead(STRING_HASH("ShadowMapArray"));
				b.DeclareTextureSRVRead(STRING_HASH("ContactShadowMap"));
				b.DeclareTextureSRVRead(STRING_HASH("AmbientOcclusionMap"));

				b.DeclareTextureRTVWrite(STRING_HASH("LightingScene"));
				b.SetClearColor(STRING_HASH("LightingScene"), 0.f, 0.f, 0.f, 1.f);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pLightingPSO, "Lighting PSO is null. (onCreated must have initialized it)");
				ASSERT(m_pLightingSRB, "Lighting SRB is null. (onCreated must have initialized it)");

				auto bindTexture = [this](const char* name, ITextureView* srv)
				{
					if (auto var = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, name))
						var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexture("g_GBuffer0", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer0_Albedo")));
				bindTexture("g_GBuffer1", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
				bindTexture("g_GBuffer2", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer2_MRAO")));
				bindTexture("g_GBuffer3", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer3_Emissive")));
				bindTexture("g_GBufferDepth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexture("g_ContactShadowMap", ctx.pRegistry->GetTextureSRV(STRING_HASH("ContactShadowMap")));
				bindTexture("g_AmbientOcclusionMap", ctx.pRegistry->GetTextureSRV(STRING_HASH("AmbientOcclusionMap")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pLightingPSO);
				pCtx->CommitShaderResources(m_pLightingSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				pCtx->Draw(da);
			},
				[this, &renderer]()
			{
				// ------------------------------------------------------------
				// onCreated: create PSO/SRB
				// ------------------------------------------------------------
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc.Name = "Lighting PSO";
				psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
				gp.RasterizerDesc.FrontCounterClockwise = true;
				gp.DepthStencilDesc.DepthEnable = false;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "Lighting VS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.FilePath = m_LightingVS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "Lighting PS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.FilePath = m_LightingPS.c_str();

				renderer.CreateShader(vsCI, &psoCi.pVS);
				renderer.CreateShader(psCI, &psoCi.pPS);

				psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_GBuffer0",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_GBuffer1",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_GBuffer2",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_GBuffer3",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_GBufferDepth", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_ContactShadowMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_AmbientOcclusionMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};

				psoCi.PSODesc.ResourceLayout.Variables = vars;
				psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

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

				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pLightingPSO = renderer.AcquirePipelineState(STRING_HASH("LightingScene"), psoCi);
				ASSERT(m_pLightingPSO, "AcquirePipelineState(LightingScene) failed.");

				m_pLightingPSO->CreateShaderResourceBinding(&m_pLightingSRB, true);
				ASSERT(m_pLightingSRB, "Lighting SRB create failed.");
			});
	}
} // namespace shz
