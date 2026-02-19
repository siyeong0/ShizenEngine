#include "pch.h"
#include "Engine/RenderSystem/Public/ContactShadowSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	void ContactShadowSystem::Initialize(Renderer& renderer)
	{
		{
			TextureDesc td = {};
			td.Name = "ScreenSpaceShadow";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;
			td.Format = TEX_FORMAT_R8_UNORM;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT; 
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("ScreenSpaceShadow"), td);
		}
	}

	void ContactShadowSystem::InstallPasses(Renderer& renderer)
	{
		renderer.AddPass(
			"ScreenSpaceShadow",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureRTVWrite(STRING_HASH("ScreenSpaceShadow"));
				
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.SetClearColor(STRING_HASH("ScreenSpaceShadow"), 1.f, 1.f, 1.f, 1.f);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pSSSPSO, "SSS PSO is null. (onCreated must have initialized it)");
				ASSERT(m_pSSSSRB, "SSS SRB is null. (onCreated must have initialized it)");

				auto bindTexturePS = [this](const char* name, ITextureView* srv)
				{
					if (auto var = m_pSSSSRB->GetVariableByName(SHADER_TYPE_PIXEL, name))
						var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexturePS("g_GBufferDepth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexturePS("g_GBufferNormal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pSSSPSO);
				pCtx->CommitShaderResources(m_pSSSSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				pCtx->Draw(da);
			},
				[this, &renderer]()
			{
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc.Name = "ScreenSpaceShadow PSO";
				psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
				gp.RasterizerDesc.FrontCounterClockwise = true;
				gp.DepthStencilDesc.DepthEnable = false;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "SSS VS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.FilePath = m_FullscreenVS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "SSS PS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.FilePath = m_SSSPS.c_str();

				renderer.CreateShader(vsCI, &psoCi.pVS);
				renderer.CreateShader(psCI, &psoCi.pPS);

				psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_GBufferDepth",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "g_GBufferNormal", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				psoCi.PSODesc.ResourceLayout.Variables = vars;
				psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

				SamplerDesc pointWrap =
				{
					FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
					TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
				};
				SamplerDesc pointClamp =
				{
					FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};

				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_PointWrapSampler", pointWrap },
					{ SHADER_TYPE_PIXEL, "g_PointClampSampler", pointClamp },
				};
				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pSSSPSO = renderer.AcquirePipelineState(STRING_HASH("ScreenSpaceShadow"), psoCi);
				ASSERT(m_pSSSPSO, "AcquirePipelineState(ScreenSpaceShadow) failed.");

				m_pSSSPSO->CreateShaderResourceBinding(&m_pSSSSRB, true);
				ASSERT(m_pSSSSRB, "SSS SRB create failed.");
			});
	}
} // namespace shz
