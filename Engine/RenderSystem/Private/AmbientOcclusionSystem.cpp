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
		const uint32 fullW = renderer.GetWidth();
		const uint32 fullH = renderer.GetHeight();

		const uint32 halfW = (fullW + 1u) / 2u;
		const uint32 halfH = (fullH + 1u) / 2u;

		// ------------------------------------------------------------
		// Half-res raw AO (UAV + SRV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "AmbientOcclusionMapHalfRaw";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = halfW;
			td.Height = halfH;
			td.MipLevels = 1;

			// UAV 호환/정밀도/필터링 안정성 때문에 R16_FLOAT 추천
			td.Format = TEX_FORMAT_R16_FLOAT;

			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("AmbientOcclusionMapHalfRaw"), td);
		}

		// ------------------------------------------------------------
		// Half-res blur intermediate (UAV + SRV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "AmbientOcclusionMapHalfBlurX";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = halfW;
			td.Height = halfH;
			td.MipLevels = 1;
			td.Format = TEX_FORMAT_R16_FLOAT;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("AmbientOcclusionMapHalfBlurX"), td);
		}

		// ------------------------------------------------------------
		// Half-res blur final (UAV + SRV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "AmbientOcclusionMapHalf";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = halfW;
			td.Height = halfH;
			td.MipLevels = 1;
			td.Format = TEX_FORMAT_R16_FLOAT;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("AmbientOcclusionMapHalf"), td);
		}

		// ------------------------------------------------------------
		// Full-res final AO (UAV + SRV)
		// ------------------------------------------------------------
		{
			TextureDesc td = {};
			td.Name = "AmbientOcclusionMap";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = fullW;
			td.Height = fullH;
			td.MipLevels = 1;

			// 최종도 R16_FLOAT로 두는게 합성/필터링에 유리
			// (원하면 마지막에 R8_UNORM으로 변환 패스 추가 가능)
			td.Format = TEX_FORMAT_R16_FLOAT;

			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

			renderer.AddTexture(STRING_HASH("AmbientOcclusionMap"), td);
		}
	}

	void AmbientOcclusionSystem::InstallPasses(Renderer& renderer)
	{
		// =====================================================================
		// Pass 0: Half-res GTAO (Compute) -> AmbientOcclusionMapHalfRaw
		// =====================================================================
		renderer.AddPass(
			"AmbientOcclusion.GTAO.HalfRes",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.DeclareTextureUAV(STRING_HASH("AmbientOcclusionMapHalfRaw"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGTAOCSO, "GTAO CSO is null.");
				ASSERT(m_pGTAOSRB, "GTAO SRB is null.");

				auto bindTexCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pGTAOSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexCS("g_GBufferDepth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexCS("g_GBufferNormal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
				bindTexCS("g_OutAO", ctx.pRegistry->GetTextureUAV(STRING_HASH("AmbientOcclusionMapHalfRaw")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pGTAOCSO);
				pCtx->CommitShaderResources(m_pGTAOSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 halfW = (renderer.GetWidth() + 1u) / 2u;
				const uint32 halfH = (renderer.GetHeight() + 1u) / 2u;

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (halfW + AO_GROUP_SIZE_X - 1) / AO_GROUP_SIZE_X;
				dispatch.ThreadGroupCountY = (halfH + AO_GROUP_SIZE_Y - 1) / AO_GROUP_SIZE_Y;
				dispatch.ThreadGroupCountZ = 1;

				pCtx->DispatchCompute(dispatch);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "GTAO_CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GTAOCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "GTAO CS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_GTAO_CS";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_GBufferDepth",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_GBufferNormal", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_OutAO",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

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
					{ SHADER_TYPE_COMPUTE, "g_PointWrapSampler",   pointWrap  },
					{ SHADER_TYPE_COMPUTE, "g_PointClampSampler",  pointClamp  },
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pGTAOCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pGTAOCSO, "AcquireCompute(GTAO_CS) failed");

				m_pGTAOCSO->CreateShaderResourceBinding(&m_pGTAOSRB, true);
				ASSERT(m_pGTAOSRB, "GTAO SRB create failed");
			});

		// =====================================================================
		// Pass 1: Half-res Bilateral Blur X (Compute) -> AmbientOcclusionMapHalfBlurX
		// =====================================================================
		renderer.AddPass(
			"AmbientOcclusion.BlurX.HalfRes",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("AmbientOcclusionMapHalfRaw"));
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.DeclareTextureUAV(STRING_HASH("AmbientOcclusionMapHalfBlurX"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pBlurXCSO, "AO blur X CSO is null.");
				ASSERT(m_pBlurXSRB, "AO blur X SRB is null.");

				auto bindTexCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pBlurXSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexCS("g_Src", ctx.pRegistry->GetTextureSRV(STRING_HASH("AmbientOcclusionMapHalfRaw")));
				bindTexCS("g_Depth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexCS("g_Normal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
				bindTexCS("g_Dst", ctx.pRegistry->GetTextureUAV(STRING_HASH("AmbientOcclusionMapHalfBlurX")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pBlurXCSO);
				pCtx->CommitShaderResources(m_pBlurXSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 halfW = (renderer.GetWidth() + 1u) / 2u;
				const uint32 halfH = (renderer.GetHeight() + 1u) / 2u;

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (halfW + BLUR_GROUP_SIZE_X - 1) / BLUR_GROUP_SIZE_X;
				dispatch.ThreadGroupCountY = (halfH + BLUR_GROUP_SIZE_Y - 1) / BLUR_GROUP_SIZE_Y;
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
		// Pass 2: Half-res Bilateral Blur Y (Compute) -> AmbientOcclusionMapHalf
		// =====================================================================
		renderer.AddPass(
			"AmbientOcclusion.BlurY.HalfRes",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("AmbientOcclusionMapHalfBlurX"));
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.DeclareTextureUAV(STRING_HASH("AmbientOcclusionMapHalf"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pBlurYCSO, "AO blur Y CSO is null.");
				ASSERT(m_pBlurYSRB, "AO blur Y SRB is null.");

				auto bindTexCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pBlurYSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexCS("g_Src", ctx.pRegistry->GetTextureSRV(STRING_HASH("AmbientOcclusionMapHalfBlurX")));
				bindTexCS("g_Depth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexCS("g_Normal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
				bindTexCS("g_Dst", ctx.pRegistry->GetTextureUAV(STRING_HASH("AmbientOcclusionMapHalf")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pBlurYCSO);
				pCtx->CommitShaderResources(m_pBlurYSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 halfW = (renderer.GetWidth() + 1u) / 2u;
				const uint32 halfH = (renderer.GetHeight() + 1u) / 2u;

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (halfW + BLUR_GROUP_SIZE_X - 1) / BLUR_GROUP_SIZE_X;
				dispatch.ThreadGroupCountY = (halfH + BLUR_GROUP_SIZE_Y - 1) / BLUR_GROUP_SIZE_Y;
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

		// =====================================================================
		// Pass 3: Bilateral Upsample (Compute) Half -> Full -> AmbientOcclusionMap
		// =====================================================================
		renderer.AddPass(
			"AmbientOcclusion.Upsample",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("AmbientOcclusionMapHalf"));
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth"));
				b.DeclareTextureSRVRead(STRING_HASH("GBuffer1_Normal"));

				b.DeclareTextureUAV(STRING_HASH("AmbientOcclusionMap"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pUpsampleCSO, "AO upsample CSO is null.");
				ASSERT(m_pUpsampleSRB, "AO upsample SRB is null.");

				auto bindTexCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pUpsampleSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindTexCS("g_AOHalf", ctx.pRegistry->GetTextureSRV(STRING_HASH("AmbientOcclusionMapHalf")));
				bindTexCS("g_Depth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
				bindTexCS("g_Normal", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
				bindTexCS("g_OutAO", ctx.pRegistry->GetTextureUAV(STRING_HASH("AmbientOcclusionMap")));

				IDeviceContext* pCtx = ctx.pImmediateContext;

				pCtx->SetPipelineState(m_pUpsampleCSO);
				pCtx->CommitShaderResources(m_pUpsampleSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 fullW = renderer.GetWidth();
				const uint32 fullH = renderer.GetHeight();

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (fullW + UPSAMPLE_GROUP_SIZE_X - 1) / UPSAMPLE_GROUP_SIZE_X;
				dispatch.ThreadGroupCountY = (fullH + UPSAMPLE_GROUP_SIZE_Y - 1) / UPSAMPLE_GROUP_SIZE_Y;
				dispatch.ThreadGroupCountZ = 1;

				pCtx->DispatchCompute(dispatch);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "AO_Upsample_CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_UpsampleCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "AO upsample CS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_AO_Upsample";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_AOHalf", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Depth",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Normal", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_OutAO",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

				m_pUpsampleCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pUpsampleCSO, "AcquireCompute(AO_Upsample) failed");

				m_pUpsampleCSO->CreateShaderResourceBinding(&m_pUpsampleSRB, true);
				ASSERT(m_pUpsampleSRB, "AO upsample SRB create failed");
			});
	}
} // namespace shz