#include "pch.h"
#include "Engine/RenderSystem/Public/AmbientOcclusionSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/GraphicsTools/Public/ShaderMacroHelper.hpp"

namespace shz
{
	void AmbientOcclusionSystem::Initialize(Renderer& renderer)
	{
		// ------------------------------------------------------------
		// Pass0 output: raw AO (RTV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "AmbientOcclusionMapRaw";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;
			td.Format = TEX_FORMAT_R8_UNORM;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("AmbientOcclusionMapRaw"), td);
		}

		// ------------------------------------------------------------
		// Pass1 output: blur X intermediate (UAV + SRV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "AmbientOcclusionMapBlurX";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;

			// NOTE:
			// R8_UNORM UAV가 백엔드/드라이버에서 안 되면 TEX_FORMAT_R16_FLOAT 권장.
			td.Format = TEX_FORMAT_R8_UNORM;

			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("AmbientOcclusionMapBlurX"), td);
		}

		// ------------------------------------------------------------
		// Pass2 output: final AO (UAV + SRV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "AmbientOcclusionMap";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = renderer.GetWidth();
			td.Height = renderer.GetHeight();
			td.MipLevels = 1;

			// NOTE:
			// R8_UNORM UAV가 백엔드/드라이버에서 안 되면 TEX_FORMAT_R16_FLOAT 권장.
			td.Format = TEX_FORMAT_R8_UNORM;

			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("AmbientOcclusionMap"), td);
		}
	}

	void AmbientOcclusionSystem::InstallPasses(Renderer& renderer)
	{
		// =====================================================================
		// Pass 0: AmbientOcclusion Raw (Graphics) -> AmbientOcclusionMapRaw
		// =====================================================================
		renderer.AddPass(
			"AmbientOcclusion",
			EPassExecutionDomain::RenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureRTVWrite(STRING_HASH("AmbientOcclusionMapRaw"));

				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.SetClearColor(STRING_HASH("AmbientOcclusionMapRaw"), 1.f, 1.f, 1.f, 1.f);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pAOPSO, "AO PSO is null. (onCreated must have initialized it)");
				ASSERT(m_pAOSRB, "AO SRB is null. (onCreated must have initialized it)");

				auto bindTexturePS = [this](const char* name, ITextureView* srv)
				{
					if (auto var = m_pAOSRB->GetVariableByName(SHADER_TYPE_PIXEL, name))
						var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexturePS("g_GBufferDepth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexturePS("g_GBufferNormal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pAOPSO);
				pCtx->CommitShaderResources(m_pAOSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				pCtx->Draw(da);
			},
				[this, &renderer]()
			{
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc.Name = "AmbientOcclusion PSO";
				psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
				gp.RasterizerDesc.FrontCounterClockwise = true;
				gp.DepthStencilDesc.DepthEnable = false;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "AO VS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.FilePath = m_FullscreenVS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "AO PS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.FilePath = m_AOPS.c_str();

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
				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};

				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_PointWrapSampler",   pointWrap   },
					{ SHADER_TYPE_PIXEL, "g_PointClampSampler",  pointClamp  },
					{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
				};
				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pAOPSO = renderer.AcquirePipelineState(STRING_HASH("AmbientOcclusion"), psoCi);
				ASSERT(m_pAOPSO, "AcquirePipelineState(AmbientOcclusion) failed.");

				m_pAOPSO->CreateShaderResourceBinding(&m_pAOSRB, true);
				ASSERT(m_pAOSRB, "AO SRB create failed.");
			});

		// =====================================================================
		// Pass 1: Bilateral Blur X (Compute) -> AmbientOcclusionMapBlurX
		// =====================================================================
		renderer.AddPass(
			"AmbientOcclusion.BilateralBlurX",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("AmbientOcclusionMapRaw"));
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.DeclareTextureUAV(STRING_HASH("AmbientOcclusionMapBlurX"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pBlurXCSO, "AO blur X CSO is null. (onCreated must have initialized it)");
				ASSERT(m_pBlurXSRB, "AO blur X SRB is null. (onCreated must have initialized it)");

				auto bindTexCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pBlurXSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexCS("g_Src", ctx.pRegistry->GetTextureSRV(STRING_HASH("AmbientOcclusionMapRaw")));
				bindTexCS("g_Depth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexCS("g_Normal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
				bindTexCS("g_Dst", ctx.pRegistry->GetTextureUAV(STRING_HASH("AmbientOcclusionMapBlurX")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pBlurXCSO);
				pCtx->CommitShaderResources(m_pBlurXSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

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
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "AO_BilateralBlurX_CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_BilateralBlurCS.c_str();

				ShaderMacroHelper macros;
				macros.AddShaderMacro("AO_BLUR_HORIZONTAL", 1);
				csCI.Macros = macros;

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "AO bilateral blur X CS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_AO_BilateralBlurX";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_Src",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Depth",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Normal", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Dst",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc pointClamp =
				{
					FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};

				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_PointClampSampler",  pointClamp  },
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pBlurXCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pBlurXCSO, "AcquireCompute(AO_BilateralBlurX) failed");

				m_pBlurXCSO->CreateShaderResourceBinding(&m_pBlurXSRB, true);
				ASSERT(m_pBlurXSRB, "AO blur X SRB create failed");
			});

		// =====================================================================
		// Pass 2: Bilateral Blur Y (Compute) -> AmbientOcclusionMap (final)
		// =====================================================================
		renderer.AddPass(
			"AmbientOcclusion.BilateralBlurY",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("AmbientOcclusionMapBlurX"));
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.DeclareTextureUAV(STRING_HASH("AmbientOcclusionMap"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pBlurYCSO, "AO blur Y CSO is null. (onCreated must have initialized it)");
				ASSERT(m_pBlurYSRB, "AO blur Y SRB is null. (onCreated must have initialized it)");

				auto bindTexCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pBlurYSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexCS("g_Src", ctx.pRegistry->GetTextureSRV(STRING_HASH("AmbientOcclusionMapBlurX")));
				bindTexCS("g_Depth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexCS("g_Normal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
				bindTexCS("g_Dst", ctx.pRegistry->GetTextureUAV(STRING_HASH("AmbientOcclusionMap")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pBlurYCSO);
				pCtx->CommitShaderResources(m_pBlurYSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

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
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "AO_BilateralBlurY_CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_BilateralBlurCS.c_str();

				ShaderMacroHelper macros;
				macros.AddShaderMacro("AO_BLUR_VERTICAL", 1);
				csCI.Macros = macros;

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "AO bilateral blur Y CS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_AO_BilateralBlurY";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_Src",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Depth",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Normal", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Dst",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc pointClamp =
				{
					FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};

				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_PointClampSampler",  pointClamp  },
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pBlurYCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pBlurYCSO, "AcquireCompute(AO_BilateralBlurY) failed");

				m_pBlurYCSO->CreateShaderResourceBinding(&m_pBlurYSRB, true);
				ASSERT(m_pBlurYSRB, "AO blur Y SRB create failed");
			});
	}
} // namespace shz
