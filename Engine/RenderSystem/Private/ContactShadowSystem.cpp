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
		// ------------------------------------------------------------
		// Pass1 output: raw contact shadow (RTV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "ContactShadowMapRaw";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;
			td.Format = TEX_FORMAT_R8_UNORM;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("ContactShadowMapRaw"), td);
		}

		// ------------------------------------------------------------
		// Pass2 output: blurred contact shadow (UAV + SRV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "ContactShadowMap";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;

			// NOTE:
			// If R8_UNORM UAV binding fails on your backend/driver,
			// change this to TEX_FORMAT_R16_FLOAT.
			td.Format = TEX_FORMAT_R8_UNORM;

			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;

			// UAV needed for compute output
			td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("ContactShadowMap"), td);
		}
	}

	void ContactShadowSystem::InstallPasses(Renderer& renderer)
	{
		// =====================================================================
		// Pass 1: ScreenSpaceShadowRaw (Graphics)
		// =====================================================================
		renderer.AddPass(
			"ContactShadow",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureRTVWrite(STRING_HASH("ContactShadowMapRaw"));

				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.SetClearColor(STRING_HASH("ContactShadowMapRaw"), 1.f, 1.f, 1.f, 1.f);
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
				psoCi.PSODesc.Name = "ContactShadow PSO";
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
					{ SHADER_TYPE_PIXEL, "g_PointWrapSampler",  pointWrap  },
					{ SHADER_TYPE_PIXEL, "g_PointClampSampler", pointClamp },
				};
				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pSSSPSO = renderer.AcquirePipelineState(STRING_HASH("ContactShadow"), psoCi);
				ASSERT(m_pSSSPSO, "AcquirePipelineState(ContactShadow) failed.");

				m_pSSSPSO->CreateShaderResourceBinding(&m_pSSSSRB, true);
				ASSERT(m_pSSSSRB, "SSS SRB create failed.");
			});

		// =====================================================================
		// Pass 2: BilinearBlur (Compute)
		// =====================================================================
		renderer.AddPass(
			"ContactShadow.BilinearBlur",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				// read raw
				b.DeclareTextureSRVRead(STRING_HASH("ContactShadowMapRaw"));

				// write final
				b.DeclareTextureUAV(STRING_HASH("ContactShadowMap"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pBlurCSO, "Blur CSO is null. (onCreated must have initialized it)");
				ASSERT(m_pBlurSRB, "Blur SRB is null. (onCreated must have initialized it)");

				auto bindTexCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pBlurSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				// SRV input
				bindTexCS("g_Src", ctx.pRegistry->GetTextureSRV(STRING_HASH("ContactShadowMapRaw")));
				// UAV output
				bindTexCS("g_Dst", ctx.pRegistry->GetTextureUAV(STRING_HASH("ContactShadowMap")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pBlurCSO);
				pCtx->CommitShaderResources(m_pBlurSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 w = renderer.GetWidth();
				const uint32 h = renderer.GetHeight();

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (w + BLUR_GROUP_SIZE_X - 1) / BLUR_GROUP_SIZE_X;
				dispatch.ThreadGroupCountY = (h + BLUR_GROUP_SIZE_Y - 1) / BLUR_GROUP_SIZE_Y;
				dispatch.ThreadGroupCountZ = 1;

				pCtx->DispatchCompute(dispatch);
			},
				[this, &renderer]()
			{
				// ---- Compile CS ----
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "SSS_BilinearBlur_CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_BlurCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "SSS blur CS compile failed");

				// ---- Create Compute PSO ----
				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_SSS_BilinearBlur";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_Src",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Dst", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				// Bilinear sampling needs linear clamp
				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};

				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pBlurCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pBlurCSO, "AcquireCompute(SSS_BilinearBlur) failed");

				m_pBlurCSO->CreateShaderResourceBinding(&m_pBlurSRB, true);
				ASSERT(m_pBlurSRB, "SSS blur SRB create failed");
			});
	}
} // namespace shz
