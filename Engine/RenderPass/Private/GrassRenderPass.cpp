#include "pch.h"
#include "Engine/RenderPass/Public/GrassRenderPass.h"
#include "Engine/Renderer/Public/IMaterialStaticBinder.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	GrassRenderPass::GrassRenderPass(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(ctx.pDepthDsv, "GrassRenderPass requires Depth DSV (ctx.pDepthDsv).");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");
		ASSERT(ctx.pGrassMaterialStaticBinder, "GrassMaterialStaticBinder is null.");

		// ------------------------------------------------------------
		// Create RenderPass (Color=LOAD, Depth=LOAD)
		// ------------------------------------------------------------
		{
			const SwapChainDesc& scDesc = ctx.pSwapChain->GetDesc();
			const TEXTURE_FORMAT colorFmt = scDesc.ColorBufferFormat;

			const TEXTURE_FORMAT depthFmt = ctx.pDepthDsv->GetDesc().Format;
			ASSERT(depthFmt != TEX_FORMAT_UNKNOWN, "Depth DSV format is unknown.");

			RenderPassAttachmentDesc atts[2] = {};

			// Color attachment: preserve lighting result => LOAD
			atts[0].Format = colorFmt;
			atts[0].SampleCount = 1;
			atts[0].LoadOp = ATTACHMENT_LOAD_OP_LOAD;
			atts[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
			atts[0].StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
			atts[0].StencilStoreOp = ATTACHMENT_STORE_OP_STORE;
			atts[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
			atts[0].FinalState = RESOURCE_STATE_RENDER_TARGET;

			// Depth attachment: preserve terrain depth => LOAD
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
			rpDesc.Name = "Grass RenderPass";
			rpDesc.AttachmentCount = 2;
			rpDesc.pAttachments = atts;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			ctx.pDevice->CreateRenderPass(rpDesc, &m_pRenderPass);
			ASSERT(m_pRenderPass, "Failed to create Grass RenderPass.");
		}

		// ------------------------------------------------------------
		// Buffers: Instance(UAV/SRV), IndirectArgs, Counter, CBs
		// ------------------------------------------------------------
		{
			// GrassInstanceBuffer (stride must match updated HLSL GrassInstance)
			{
				BufferDesc bd = {};
				bd.Name = "GrassInstanceBuffer";
				bd.Usage = USAGE_DEFAULT;
				bd.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
				bd.Mode = BUFFER_MODE_STRUCTURED;
				bd.ElementByteStride = sizeof(hlsl::GrassInstance);
				bd.Size = uint64{ m_MaxInstances } *uint64{ sizeof(hlsl::GrassInstance) };

				ctx.pDevice->CreateBuffer(bd, nullptr, &m_pGrassInstanceBuffer);
				ASSERT(m_pGrassInstanceBuffer, "CreateBuffer(GrassInstanceBuffer) failed.");
			}

			// Indirect args (RAW 20 bytes)
			{
				BufferDesc bd = {};
				bd.Name = "GrassIndirectArgs";
				bd.Usage = USAGE_DEFAULT;
				bd.BindFlags = BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS;
				bd.Mode = BUFFER_MODE_RAW;
				bd.Size = 20;

				ctx.pDevice->CreateBuffer(bd, nullptr, &m_pIndirectArgsBuffer);
				ASSERT(m_pIndirectArgsBuffer, "CreateBuffer(GrassIndirectArgs) failed.");
			}

			// Counter (RAW 4 bytes)
			{
				BufferDesc bd = {};
				bd.Name = "GrassCounter";
				bd.Usage = USAGE_DEFAULT;
				bd.BindFlags = BIND_UNORDERED_ACCESS;
				bd.Mode = BUFFER_MODE_RAW;
				bd.Size = 4;

				ctx.pDevice->CreateBuffer(bd, nullptr, &m_pCounterBuffer);
				ASSERT(m_pCounterBuffer, "CreateBuffer(GrassCounter) failed.");
			}

			// NEW: GrassGenConstantsCB (CS)
			{
				BufferDesc bd = {};
				bd.Name = "GrassGenConstantsCB";
				bd.Usage = USAGE_DYNAMIC;
				bd.BindFlags = BIND_UNIFORM_BUFFER;
				bd.CPUAccessFlags = CPU_ACCESS_WRITE;
				bd.Size = sizeof(hlsl::GrassGenConstants);

				ctx.pDevice->CreateBuffer(bd, nullptr, &m_pGrassGenConstantsCB);
				ASSERT(m_pGrassGenConstantsCB, "CreateBuffer(GrassGenConstantsCB) failed.");
			}

			// NEW: GrassRenderConstantsCB (VS/PS)
			{
				BufferDesc bd = {};
				bd.Name = "GrassRenderConstantsCB";
				bd.Usage = USAGE_DYNAMIC;
				bd.BindFlags = BIND_UNIFORM_BUFFER;
				bd.CPUAccessFlags = CPU_ACCESS_WRITE;
				bd.Size = sizeof(hlsl::GrassRenderConstants);

				ctx.pDevice->CreateBuffer(bd, nullptr, &m_pGrassRenderConstantsCB);
				ASSERT(m_pGrassRenderConstantsCB, "CreateBuffer(GrassRenderConstantsCB) failed.");
			}
		}

		// ------------------------------------------------------------
		// Compute PSO #1: GenerateGrassInstances
		// - expects: FRAME_CONSTANTS + GRASS_GEN_CONSTANTS + g_OutInstances + g_Counter + g_HeightMap + sampler
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.Name = "GrassGenerateInstancesCS";
			sci.EntryPoint = "GenerateGrassInstances";
			sci.FilePath = "GrassBuildInstances.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(GrassGenerateInstancesCS) failed.");

			ComputePipelineStateCreateInfo psoCI = {};
			psoCI.PSODesc.Name = "PSO_GrassGenerateInstances";
			psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

			PipelineResourceLayoutDesc& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_OutInstances",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_Counter",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_HeightMap",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

				// NEW CB (mutable so we can set buffer)
				{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			};
			rl.Variables = vars;
			rl.NumVariables = _countof(vars);

			SamplerDesc linearWrap =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			ImmutableSamplerDesc samplers[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_LinearWrapSampler", linearWrap },
			};

			rl.ImmutableSamplers = samplers;
			rl.NumImmutableSamplers = _countof(samplers);

			psoCI.pCS = pCS;

			ctx.pDevice->CreateComputePipelineState(psoCI, &m_pGenCSO);
			ASSERT(m_pGenCSO, "CreateComputePipelineState(PSO_GrassGenerateInstances) failed.");

			ctx.pGrassMaterialStaticBinder->BindStatics(m_pGenCSO);

			m_pGenCSO->CreateShaderResourceBinding(&m_pGenCSRB, true);
			ASSERT(m_pGenCSRB, "Create SRB for GrassGenerateInstances failed.");

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstances"))
			{
				var->Set(m_pGrassInstanceBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
			}

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
			{
				var->Set(m_pCounterBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
			}

			// Bind CB
			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
			{
				var->Set(m_pGrassGenConstantsCB);
			}
		}

		// ------------------------------------------------------------
		// Compute PSO #2: WriteIndirectArgs (1 thread)
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.UseCombinedTextureSamplers = false;

			sci.Desc.Name = "GrassWriteIndirectArgsCS";
			sci.EntryPoint = "WriteIndirectArgs";
			sci.FilePath = "GrassBuildInstances.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(GrassWriteIndirectArgsCS) failed.");

			ComputePipelineStateCreateInfo psoCI = {};
			psoCI.PSODesc.Name = "PSO_GrassWriteIndirectArgs";
			psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

			PipelineResourceLayoutDesc& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_IndirectArgs", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_Counter",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			};
			rl.Variables = vars;
			rl.NumVariables = _countof(vars);

			psoCI.pCS = pCS;

			ctx.pDevice->CreateComputePipelineState(psoCI, &m_pArgsCSO);
			ASSERT(m_pArgsCSO, "CreateComputePipelineState(PSO_GrassWriteIndirectArgs) failed.");

			ctx.pGrassMaterialStaticBinder->BindStatics(m_pArgsCSO);

			m_pArgsCSO->CreateShaderResourceBinding(&m_pArgsCSRB, true);
			ASSERT(m_pArgsCSRB, "Create SRB for GrassWriteIndirectArgs failed.");

			if (auto* varArgs = m_pArgsCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectArgs"))
			{
				varArgs->Set(m_pIndirectArgsBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
			}
			if (auto* varCounter = m_pArgsCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
			{
				varCounter->Set(m_pCounterBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
			}
		}

		// ------------------------------------------------------------
		// Grass PSO
		// - g_GrassInstances is MUTABLE
		// - NEW: GRASS_RENDER_CONSTANTS as MUTABLE (VS/PS)
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

			PipelineResourceLayoutDesc& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				// Instances SRV (VS)
				{ SHADER_TYPE_VERTEX, "g_GrassInstances",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

				// NEW render constants CB (VS + PS, set same buffer)
				{ SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_PIXEL,  "GRASS_RENDER_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

				// Existing resources (PS)
				{ SHADER_TYPE_PIXEL, "g_BaseColorTex",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_PIXEL, "g_ShadowMap",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

				{ SHADER_TYPE_PIXEL, "g_IrradianceIBLTex",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_PIXEL, "g_SpecularIBLTex",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_PIXEL, "g_BrdfIBLTex",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

			GraphicsPipelineDesc& gp = psoCI.GraphicsPipeline;
			gp.pRenderPass = m_pRenderPass;
			gp.SubpassIndex = 0;

			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			// Explicit render pass used => NumRenderTargets must be 0
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

			ctx.pDevice->CreatePipelineState(psoCI, &m_pGrassPSO);
			ASSERT(m_pGrassPSO, "CreatePipelineState(PSO_Grass) failed.");

			ctx.pGrassMaterialStaticBinder->BindStatics(m_pGrassPSO);

			m_pGrassPSO->CreateShaderResourceBinding(&m_pGrassSRB, true);
			ASSERT(m_pGrassSRB, "Create SRB for Grass failed.");

			// Instances SRV
			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances"))
			{
				var->Set(m_pGrassInstanceBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
			}

			// Bind render constants CB to both stages if present
			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS"))
			{
				var->Set(m_pGrassRenderConstantsCB);
			}
			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "GRASS_RENDER_CONSTANTS"))
			{
				var->Set(m_pGrassRenderConstantsCB);
			}

			// Shadow map
			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap"))
			{
				var->Set(ctx.pShadowMapSrv);
			}

			// IBL
			if (auto var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceIBLTex"))
			{
				if (ctx.pEnvDiffuseTex)
				{
					var->Set(ctx.pEnvDiffuseTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			}
			if (auto var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SpecularIBLTex"))
			{
				if (ctx.pEnvSpecularTex)
				{
					var->Set(ctx.pEnvSpecularTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			}
			if (auto var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BrdfIBLTex"))
			{
				if (ctx.pEnvBrdfTex)
				{
					var->Set(ctx.pEnvBrdfTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			}
		}

		// ------------------------------------------------------------
		// Framebuffer for current back buffer
		// ------------------------------------------------------------
		buildFramebufferForCurrentBackBuffer(ctx);
	}

	GrassRenderPass::~GrassRenderPass()
	{
		m_pFramebuffer.Release();
		m_pRenderPass.Release();

		m_pGenCSRB.Release();
		m_pGenCSO.Release();

		m_pArgsCSRB.Release();
		m_pArgsCSO.Release();

		m_pGrassSRB.Release();
		m_pGrassPSO.Release();

		m_pGrassInstanceBuffer.Release();
		m_pIndirectArgsBuffer.Release();
		m_pCounterBuffer.Release();

		m_pGrassGenConstantsCB.Release();
		m_pGrassRenderConstantsCB.Release();
	}

	void GrassRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDepthDsv, "GrassRenderPass requires Depth DSV (ctx.pDepthDsv).");
		buildFramebufferForCurrentBackBuffer(ctx);
	}

	void GrassRenderPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(ctx.pDrawCB, "DrawCB is null.");
		ASSERT(m_pRenderPass, "Grass RenderPass is null.");
		ASSERT(m_pFramebuffer, "Grass Framebuffer is null.");

		if (!ctx.HeightMap.Texture)
			return;

		IDeviceContext* pContext = ctx.pImmediateContext;

		// ---------------------------------------------------------------------
		// (0) Reset counter + init indirect args (GPU)
		// ---------------------------------------------------------------------
		{
			const uint32 zero = 0;
			pContext->UpdateBuffer(
				m_pCounterBuffer,
				0,
				sizeof(uint32),
				&zero,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			// Keep current behavior (doesn't matter: WriteIndirectArgs will overwrite)
			const uint32 args[5] = { 6u, 0u, 0u, 0u, 0u };
			pContext->UpdateBuffer(
				m_pIndirectArgsBuffer,
				0,
				sizeof(args),
				args,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		}

		// GrassGenConstants
		{
			MapHelper<hlsl::GrassGenConstants> map(
				pContext,
				m_pGrassGenConstantsCB,
				MAP_WRITE,
				MAP_FLAG_DISCARD);

			map->HeightScale = 100.0f;
			map->HeightOffset = 0.0f;
			map->YOffset = 0.0f;

			map->HFWidth = 1025;
			map->HFHeight = 1025;
			map->CenterXZ = 1;

			map->SpacingX = 1.0f;
			map->SpacingZ = 1.0f;

			// Chunk placement
			map->ChunkSize = 4.0f;
			map->ChunkHalfExtent = 32;
			map->SamplesPerChunk = 1024;
			map->Jitter = 0.95f;

			map->SpawnProb = 0.95f;
			map->SpawnRadius = 500.0f;

			map->MinScale = 0.7f;
			map->MaxScale = 3.1f;

			map->BendStrengthMin = 0.95f;
			map->BendStrengthMax = 1.05f;

			map->SeedSalt = 0xA53A9E37u;
		}

		// GrassRenderConstants
		{
			MapHelper<hlsl::GrassRenderConstants> map(
				pContext,
				m_pGrassRenderConstantsCB,
				MAP_WRITE,
				MAP_FLAG_DISCARD);

			map->BaseColorFactor = float4{ 0.18f, 0.78f, 0.22f, 1.0f };
			map->Tint = float4{ 1.05f, 1.00f, 0.95f, 1.0f };

			map->AlphaCut = 0.5f;

			map->Ambient = 0.30f;
			map->ShadowStregth = 0.18f;
			map->DirectLightStrength = 0.22f;

			map->WindDirXZ = float2{ 0.80f, 0.60f };
			map->WindStrength = 0.85f;
			map->WindSpeed = 0.75f;

			map->WindFreq = 0.055f;
			map->WindGust = 0.22f;
			map->MaxBendAngle = 1.50f;
		}

		// ---------------------------------------------------------------------
		// (1) Compute: GenerateGrassInstances
		// ---------------------------------------------------------------------
		{
			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightMap"))
			{
				var->Set(ctx.HeightMap.Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			}

			StateTransitionDesc tr[] =
			{
				{ m_pGrassInstanceBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pCounterBuffer,       RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.HeightMap.Texture,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,  STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(tr), tr);

			pContext->SetPipelineState(m_pGenCSO);
			pContext->CommitShaderResources(m_pGenCSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			// Preserve current behavior: same dispatch grid as before
			DispatchComputeAttribs disp = {};
			disp.ThreadGroupCountX = (2u * 32u + 8u - 1u) / 8u;
			disp.ThreadGroupCountY = (2u * 32u + 8u - 1u) / 8u;
			disp.ThreadGroupCountZ = 1;

			pContext->DispatchCompute(disp);
		}

		// ---------------------------------------------------------------------
		// (1.5) Compute: WriteIndirectArgs
		// ---------------------------------------------------------------------
		{
			StateTransitionDesc tr[] =
			{
				{ m_pIndirectArgsBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pCounterBuffer,      RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(tr), tr);

			pContext->SetPipelineState(m_pArgsCSO);
			pContext->CommitShaderResources(m_pArgsCSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			DispatchComputeAttribs disp = {};
			disp.ThreadGroupCountX = 1;
			disp.ThreadGroupCountY = 1;
			disp.ThreadGroupCountZ = 1;

			pContext->DispatchCompute(disp);
		}

		// ---------------------------------------------------------------------
		// (2) Prepare for graphics: SRV/Indirect
		// ---------------------------------------------------------------------
		{
			StateTransitionDesc tr2[] =
			{
				{ m_pGrassInstanceBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,   STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pIndirectArgsBuffer,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDIRECT_ARGUMENT, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(tr2), tr2);
		}

		// ------------------------------------------------------------
		// (3) Graphics 준비: VB/IB 상태 전이 (RenderPass 밖)
		// ------------------------------------------------------------
		{
			StateTransitionDesc trGfx[] =
			{
				{ m_pGrassInstanceBuffer,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,   STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pIndirectArgsBuffer,   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDIRECT_ARGUMENT, STATE_TRANSITION_FLAG_UPDATE_STATE },

				// IMPORTANT: transitions must be OUTSIDE render pass
				{ m_GrassMesh.VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GrassMesh.IndexBuffer,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER,  STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(trGfx), trGfx);
		}

		// ---------------------------------------------------------------------
		// (4) Begin render pass + DrawIndexedIndirect
		// ---------------------------------------------------------------------
		{
			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_pRenderPass;
			rp.pFramebuffer = m_pFramebuffer;
			rp.ClearValueCount = 0;
			rp.pClearValues = nullptr;

			pContext->BeginRenderPass(rp);

			pContext->SetPipelineState(m_pGrassPSO);
			pContext->CommitShaderResources(m_pGrassSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			// VB/IB bind (keep VERIFY - state already transitioned above)
			{
				ASSERT(m_GrassMesh.VertexBuffer, "Grass mesh VB is null.");
				ASSERT(m_GrassMesh.IndexBuffer, "Grass mesh IB is null.");

				IBuffer* ppVertexBuffers[] = { m_GrassMesh.VertexBuffer };
				uint64 offsets[] = { 0 };

				pContext->SetVertexBuffers(
					0, 1, ppVertexBuffers, offsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pContext->SetIndexBuffer(
					m_GrassMesh.IndexBuffer, 0,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			}

			// Per-draw: StartInstanceLocation = 0
			{
				MapHelper<hlsl::DrawConstants> map(pContext, ctx.pDrawCB, MAP_WRITE, MAP_FLAG_DISCARD);
				map->StartInstanceLocation = 0;
			}

			DrawIndexedIndirectAttribs ia = {};
			ia.IndexType = m_GrassMesh.IndexType;
			ia.pAttribsBuffer = m_pIndirectArgsBuffer;
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
		(void)width;
		(void)height;

		m_pFramebuffer.Release();
	}

	void GrassRenderPass::SetGrassModel(RenderPassContext& ctx, const StaticMeshRenderData& mesh)
	{
		(void)ctx;
		ASSERT(m_pGrassPSO, "PSO is null.");
		m_GrassMesh = mesh;
	}

	bool GrassRenderPass::buildFramebufferForCurrentBackBuffer(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(ctx.pDepthDsv, "GrassRenderPass requires Depth DSV (ctx.pDepthDsv).");
		ASSERT(m_pRenderPass, "Grass render pass is null.");

		ITextureView* pRTV = ctx.pLightingRtv;
		ITextureView* pDSV = ctx.pDepthDsv;
		ASSERT(pRTV, "BackBuffer RTV is null.");
		ASSERT(pDSV, "Depth DSV is null.");

		FramebufferDesc fbDesc = {};
		fbDesc.Name = "Grass Framebuffer";
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
