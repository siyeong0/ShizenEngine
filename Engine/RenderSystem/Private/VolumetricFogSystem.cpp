#include "pch.h"
#include "Engine/RenderSystem/Public/VolumetricFogSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	static inline bool IsEvenFrame(const RenderPassContext& ctx)
	{
		return (ctx.FrameIndex & 1u) == 0u;
	}

	void VolumetricFogSystem::Initialize(Renderer& renderer)
	{
		const uint32 fogW = (renderer.GetWidth() + (m_Downsample - 1)) / m_Downsample;
		const uint32 fogH = (renderer.GetHeight() + (m_Downsample - 1)) / m_Downsample;

		auto add3D = [&](const char* name)
		{
			TextureDesc td = {};
			td.Name = name;
			td.Type = RESOURCE_DIM_TEX_3D;
			td.Width = fogW;
			td.Height = fogH;
			td.Depth = m_ZSlices;
			td.MipLevels = 1;
			td.Format = TEX_FORMAT_RGBA16_FLOAT;
			td.Usage = USAGE_DEFAULT;
			td.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
			td.SampleCount = 1;

			renderer.AddTexture(STRING_HASH(name), td);
		};

		add3D("FogVolume_Scatter");
		add3D("FogVolume_Integrated");
		add3D("FogVolume_Final");

		// History ping-pong (3D)
		add3D("FogVolume_History0");
		add3D("FogVolume_History1");

		// Constant buffer
		{
			BufferDesc bd = {};
			bd.Name = "FogConstants";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			bd.Size = sizeof(hlsl::FogConstants);

			renderer.AddBuffer(STRING_HASH("FogConstantsCB"), bd);
			renderer.RegisterStaticBufferCBV("FOG_CONSTANTS", STRING_HASH("FogConstantsCB"));

			hlsl::FogConstants fogCB;
			fogCB.BaseDensity = 0.001f;
			fogCB.DensityScale = 1.0f;
			fogCB.ExtinctionScale = 0.85f; 
			fogCB.Albedo = 0.92f; 

			fogCB.AnisotropyG = 0.1f; 
			fogCB.PhaseBoost = 1.0f;

			fogCB.FogColor = float3(0.78f, 0.86f, 1.00f);

			fogCB.MaxDistance = 2500.0f;

			fogCB.BaseHeight = 0.0f;
			fogCB.HeightFogStart = 0.0f;
			fogCB.HeightFalloff = 0.0f;

			fogCB.TemporalAlpha = 0.12f;
			fogCB.HistoryRejectThreshold = 0.20f;
			fogCB.JitterStrength = 0.35f;
			fogCB.HistoryClampExpand = 0.04f;
			fogCB.TemporalVelocityScale = 400.0f;

			renderer.UpdateBuffer(STRING_HASH("FogConstantsCB"), fogCB);
		}
	}

	void VolumetricFogSystem::InstallPasses(Renderer& renderer)
	{
		// =============================================================
		// Pass0: Scatter (Compute)
		//   Reads: ShadowMapArray (directional visibility)
		//   Writes: FogVolume_Scatter (UAV)
		// =============================================================
		renderer.AddPass(
			"Fog.Scatter",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("ShadowMapArray"));
				b.DeclareTextureUAV(STRING_HASH("FogVolume_Scatter"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "Fog.Scatter ctx invalid.");
				ASSERT(m_pScatterPSO && m_pScatterSRB, "Fog.Scatter PSO/SRB null.");

				auto bindCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pScatterSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindCS("g_ShadowMapArray", ctx.pRegistry->GetTextureSRV(STRING_HASH("ShadowMapArray")));
				bindCS("g_OutScatter", ctx.pRegistry->GetTextureUAV(STRING_HASH("FogVolume_Scatter")));

				IDeviceContext* pCtx = ctx.pImmediateContext;
				pCtx->SetPipelineState(m_pScatterPSO);
				pCtx->CommitShaderResources(m_pScatterSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 fogW = (renderer.GetWidth() + (m_Downsample - 1)) / m_Downsample;
				const uint32 fogH = (renderer.GetHeight() + (m_Downsample - 1)) / m_Downsample;

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (fogW + 7u) / 8u;
				dispatch.ThreadGroupCountY = (fogH + 7u) / 8u;
				dispatch.ThreadGroupCountZ = (m_ZSlices + 1u - 1u) / 1u;

				pCtx->DispatchCompute(dispatch);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "Fog Scatter CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_ScatterCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "Fog Scatter CS compile failed.");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Fog_Scatter";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_ShadowMapArray", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_OutScatter",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_COMPUTE, "g_ShadowCmpSampler",   shadowClamp },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pScatterPSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pScatterPSO, "AcquireCompute(Fog.Scatter) failed.");

				m_pScatterPSO->CreateShaderResourceBinding(&m_pScatterSRB, true);
				ASSERT(m_pScatterSRB, "Fog.Scatter SRB create failed.");
			});

		// =============================================================
		// Pass1: Integrate (Compute)
		//   Reads : FogVolume_Scatter
		//   Writes: FogVolume_Integrated (rgb=L, a=T)
		// =============================================================
		renderer.AddPass(
			"Fog.Integrate",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("FogVolume_Scatter"));
				b.DeclareTextureUAV(STRING_HASH("FogVolume_Integrated"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "Fog.Integrate ctx invalid.");
				ASSERT(m_pIntegratePSO && m_pIntegrateSRB, "Fog.Integrate PSO/SRB null.");

				auto bindCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pIntegrateSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindCS("g_ScatterVolume", ctx.pRegistry->GetTextureSRV(STRING_HASH("FogVolume_Scatter")));
				bindCS("g_OutIntegrated", ctx.pRegistry->GetTextureUAV(STRING_HASH("FogVolume_Integrated")));

				IDeviceContext* pCtx = ctx.pImmediateContext;
				pCtx->SetPipelineState(m_pIntegratePSO);
				pCtx->CommitShaderResources(m_pIntegrateSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 fogW = (renderer.GetWidth() + (m_Downsample - 1)) / m_Downsample;
				const uint32 fogH = (renderer.GetHeight() + (m_Downsample - 1)) / m_Downsample;

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (fogW + 7u) / 8u;
				dispatch.ThreadGroupCountY = (fogH + 7u) / 8u;
				dispatch.ThreadGroupCountZ = 1;

				pCtx->DispatchCompute(dispatch);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "Fog Integrate CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_IntegrateCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "Fog Integrate CS compile failed.");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Fog_Integrate";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_ScatterVolume", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_OutIntegrated", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

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

				m_pIntegratePSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pIntegratePSO, "AcquireCompute(Fog.Integrate) failed.");

				m_pIntegratePSO->CreateShaderResourceBinding(&m_pIntegrateSRB, true);
				ASSERT(m_pIntegrateSRB, "Fog.Integrate SRB create failed.");
			});

		// =============================================================
		// Pass2: Temporal (Compute)
		//   Reads : FogVolume_Integrated + History(prev)
		//   Writes: FogVolume_Final + History(curr)
		// =============================================================
		renderer.AddPass(
			"Fog.Temporal",
			EPassExecutionDomain::OutsideRenderPass,
			[](RenderPassBuilder& b)
			{
				b.DeclareTextureSRVRead(STRING_HASH("FogVolume_Integrated"));

				b.DeclareTextureUAV(STRING_HASH("FogVolume_Final"), RENDER_ACCESS_WRITE);

				b.DeclareTextureUAV(STRING_HASH("FogVolume_History0"), RENDER_ACCESS_WRITE);
				b.DeclareTextureUAV(STRING_HASH("FogVolume_History1"), RENDER_ACCESS_WRITE);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "Fog.Temporal ctx invalid.");
				ASSERT(m_pTemporalPSO && m_pTemporalSRB, "Fog.Temporal PSO/SRB null.");

				const bool even = IsEvenFrame(ctx);

				const uint64 kPrev = even ? STRING_HASH("FogVolume_History1") : STRING_HASH("FogVolume_History0");
				const uint64 kCurr = even ? STRING_HASH("FogVolume_History0") : STRING_HASH("FogVolume_History1");

				auto bindCS = [this](const char* name, ITextureView* view)
				{
					if (auto* var = m_pTemporalSRB->GetVariableByName(SHADER_TYPE_COMPUTE, name))
						var->Set(view, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				};

				bindCS("g_Integrated", ctx.pRegistry->GetTextureSRV(STRING_HASH("FogVolume_Integrated")));
				bindCS("g_HistoryPrev", ctx.pRegistry->GetTextureSRV(kPrev));

				bindCS("g_OutFinal", ctx.pRegistry->GetTextureUAV(STRING_HASH("FogVolume_Final")));
				bindCS("g_HistoryCurr", ctx.pRegistry->GetTextureUAV(kCurr));

				IDeviceContext* pCtx = ctx.pImmediateContext;
				pCtx->SetPipelineState(m_pTemporalPSO);
				pCtx->CommitShaderResources(m_pTemporalSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const uint32 fogW = (renderer.GetWidth() + (m_Downsample - 1)) / m_Downsample;
				const uint32 fogH = (renderer.GetHeight() + (m_Downsample - 1)) / m_Downsample;

				DispatchComputeAttribs dispatch = {};
				dispatch.ThreadGroupCountX = (fogW + 7u) / 8u;
				dispatch.ThreadGroupCountY = (fogH + 7u) / 8u;
				dispatch.ThreadGroupCountZ = (m_ZSlices + 1u - 1u) / 1u;

				pCtx->DispatchCompute(dispatch);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "main";
				csCI.Desc.Name = "Fog Temporal CS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_TemporalCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "Fog Temporal CS compile failed.");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Fog_Temporal";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_Integrated",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_HistoryPrev", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_OutFinal",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_HistoryCurr", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

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

				m_pTemporalPSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pTemporalPSO, "AcquireCompute(Fog.Temporal) failed.");

				m_pTemporalPSO->CreateShaderResourceBinding(&m_pTemporalSRB, true);
				ASSERT(m_pTemporalSRB, "Fog.Temporal SRB create failed.");
			});
	}
} // namespace shz