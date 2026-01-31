#include "pch.h"
#include "Engine/RenderPass/Public/GrassRenderPass.h"
#include "Engine/Renderer/Public/CommonResourceId.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	// -----------------------------------------------------------------------------
	// Helpers
	// -----------------------------------------------------------------------------
	static inline uint32 DivUp(uint32 x, uint32 d)
	{
		return (x + d - 1) / d;
	}

	static inline float2 NormalizeSafe(const float2& v, float2 fallback = float2{ 1.0f, 0.0f })
	{
		const float len2 = v.x * v.x + v.y * v.y;
		if (len2 <= 1e-12f)
			return fallback;
		const float invLen = 1.0f / std::sqrt(len2);
		return float2{ v.x * invLen, v.y * invLen };
	}

	// World XZ -> Terrain UV (0..1), matches GrassBuildInstances.hlsl mapping assumption:
	// - heightfield size = (HFWidth-1)*SpacingX, (HFHeight-1)*SpacingZ
	// - if CenterXZ==1, terrain origin is centered: origin = -0.5*size
	static inline float2 WorldXZToTerrainUV(
		const hlsl::GrassGenConstants& gen,
		const float2& worldXZ)
	{
		const float sizeX = float(std::max<int>(int(gen.HFWidth) - 1, 0)) * gen.SpacingX;
		const float sizeZ = float(std::max<int>(int(gen.HFHeight) - 1, 0)) * gen.SpacingZ;

		const float originX = (gen.CenterXZ != 0) ? (-0.5f * sizeX) : 0.0f;
		const float originZ = (gen.CenterXZ != 0) ? (-0.5f * sizeZ) : 0.0f;

		const float invSizeX = 1.0f / std::max(sizeX, 1e-6f);
		const float invSizeZ = 1.0f / std::max(sizeZ, 1e-6f);

		return float2
		{
			(worldXZ.x - originX) * invSizeX,
			(worldXZ.y - originZ) * invSizeZ
		};
	}

	static inline float WorldRadiusToUv_MinAxis(
		const hlsl::GrassGenConstants& gen,
		float radiusWorld)
	{
		const float sizeX = float(std::max<int>(int(gen.HFWidth) - 1, 0)) * gen.SpacingX;
		const float sizeZ = float(std::max<int>(int(gen.HFHeight) - 1, 0)) * gen.SpacingZ;
		const float sizeMin = std::max(std::min(sizeX, sizeZ), 1e-6f);
		return radiusWorld / sizeMin;
	}

	// -----------------------------------------------------------------------------
	// GrassRenderPass
	// -----------------------------------------------------------------------------
	GrassRenderPass::GrassRenderPass(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(ctx.pShaderSourceFactory, "ShaderSourceFactory is null.");

		// ------------------------------------------------------------
		// Create RenderPass (Color=LOAD, Depth=LOAD)
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
			// GrassInstanceBuffer
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

			// GrassGenConstantsCB (CS)
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

			// GrassRenderConstantsCB (VS/PS)
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

			// Interaction field texture (R16_FLOAT SRV/UAV)
			{
				m_pInteractionFieldTex.Release();
				m_pInteractionFieldSRV = nullptr;
				m_pInteractionFieldUAV = nullptr;

				TextureDesc td = {};
				td.Name = "InteractionField";
				td.Type = RESOURCE_DIM_TEX_2D;
				td.Width = m_InteractionW;
				td.Height = m_InteractionH;
				td.Format = TEX_FORMAT_R16_FLOAT;
				td.MipLevels = 1;
				td.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
				td.Usage = USAGE_DEFAULT;

				ctx.pDevice->CreateTexture(td, nullptr, &m_pInteractionFieldTex);
				ASSERT(m_pInteractionFieldTex, "CreateTexture(InteractionField) failed.");

				m_pInteractionFieldSRV = m_pInteractionFieldTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
				m_pInteractionFieldUAV = m_pInteractionFieldTex->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS);

				ASSERT(m_pInteractionFieldSRV && m_pInteractionFieldUAV, "InteractionField SRV/UAV is null.");
			}

			// Interaction stamps (Structured, dynamic CPU write)
			{
				BufferDesc bd = {};
				bd.Name = "InteractionStampBuffer";
				bd.Usage = USAGE_DYNAMIC;
				bd.BindFlags = BIND_SHADER_RESOURCE;
				bd.Mode = BUFFER_MODE_STRUCTURED;
				bd.ElementByteStride = sizeof(hlsl::InteractionStamp);
				bd.Size = uint64(MAX_NUM_INTERACTION_STAMPS) * uint64(sizeof(hlsl::InteractionStamp));
				bd.CPUAccessFlags = CPU_ACCESS_WRITE;

				ctx.pDevice->CreateBuffer(bd, nullptr, &m_pInteractionStampBuffer);
				ASSERT(m_pInteractionStampBuffer, "CreateBuffer(InteractionStampBuffer) failed.");
			}

			// Interaction constants
			{
				BufferDesc bd = {};
				bd.Name = "InteractionConstantsCB";
				bd.Usage = USAGE_DYNAMIC;
				bd.BindFlags = BIND_UNIFORM_BUFFER;
				bd.CPUAccessFlags = CPU_ACCESS_WRITE;
				bd.Size = uint64(sizeof(hlsl::InteractionConstants));

				ctx.pDevice->CreateBuffer(bd, nullptr, &m_pInteractionConstantsCB);
				ASSERT(m_pInteractionConstantsCB, "CreateBuffer(InteractionConstantsCB) failed.");
			}
		}

		// ------------------------------------------------------------
		// Compute PSO #1: GenerateGrassInstances
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

			auto& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_OutInstances",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_Counter",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_HeightMap",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_DensityField",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_InteractionField",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

			if (auto* var = m_pGenCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "FRAME_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(kRes_FrameCB));
			}

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

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
			{
				var->Set(m_pGrassGenConstantsCB);
			}
		}

		// ------------------------------------------------------------
		// Compute PSO #2: WriteIndirectArgs
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

			auto& rl = psoCI.PSODesc.ResourceLayout;
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

			m_pArgsCSO->CreateShaderResourceBinding(&m_pArgsCSRB, true);
			ASSERT(m_pArgsCSRB, "Create SRB for GrassWriteIndirectArgs failed.");

			if (auto* varArgs = m_pArgsCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectArgs"))
				varArgs->Set(m_pIndirectArgsBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));

			if (auto* varCounter = m_pArgsCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
				varCounter->Set(m_pCounterBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
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
				{ SHADER_TYPE_VERTEX, "g_GrassInstances",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_PIXEL,  "GRASS_RENDER_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

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

			auto& gp = psoCI.GraphicsPipeline;
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

			if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(kRes_FrameCB));
			}
			if (auto* var = m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
			{
				var->Set(ctx.pRegistry->GetBuffer(kRes_FrameCB));
			}

			m_pGrassPSO->CreateShaderResourceBinding(&m_pGrassSRB, true);
			ASSERT(m_pGrassSRB, "Create SRB for Grass failed.");

			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances"))
			{
				var->Set(m_pGrassInstanceBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
			}

			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS"))
				var->Set(m_pGrassRenderConstantsCB);
			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "GRASS_RENDER_CONSTANTS"))
				var->Set(m_pGrassRenderConstantsCB);

			if (auto* var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap"))
				var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("ShadowMap")));

			if (auto var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceIBLTex"))
			{
				if (ctx.pRegistry->GetTexture(kRes_EnvDiffuseTex))
				{
					var->Set(ctx.pRegistry->GetTextureSRV(kRes_EnvDiffuseTex), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			}
			if (auto var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SpecularIBLTex"))
			{
				if (ctx.pRegistry->GetTexture(kRes_EnvSpecularTex))
				{
					var->Set(ctx.pRegistry->GetTextureSRV(kRes_EnvSpecularTex), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			}
			if (auto var = m_pGrassSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BrdfIBLTex"))
			{
				if (ctx.pRegistry->GetTexture(kRes_EnvBrdfTex))
				{
					var->Set(ctx.pRegistry->GetTextureSRV(kRes_EnvBrdfTex), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			}
		}

		// ------------------------------------------------------------
		// Compute PSO: Interaction Decay
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.Name = "InteractionDecayCS";
			sci.EntryPoint = "DecayInteractionField";
			sci.FilePath = "InteractionFieldUpdate.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(InteractionDecayCS) failed.");

			ComputePipelineStateCreateInfo psoCI = {};
			psoCI.PSODesc.Name = "PSO_InteractionDecay";
			psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

			auto& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			};
			rl.Variables = vars;
			rl.NumVariables = _countof(vars);

			psoCI.pCS = pCS;

			ctx.pDevice->CreateComputePipelineState(psoCI, &m_pInteractionDecayCSO);
			ASSERT(m_pInteractionDecayCSO, "CreateComputePipelineState(PSO_InteractionDecay) failed.");

			m_pInteractionDecayCSO->CreateShaderResourceBinding(&m_pInteractionDecaySRB, true);
			ASSERT(m_pInteractionDecaySRB, "Create SRB for InteractionDecay failed.");

			if (auto* var = m_pInteractionDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
				var->Set(m_pInteractionConstantsCB);
		}

		// ------------------------------------------------------------
		// Compute PSO: Interaction Apply Stamps
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
			sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sci.pShaderSourceStreamFactory = ctx.pShaderSourceFactory;

			sci.Desc.Name = "InteractionApplyStampsCS";
			sci.EntryPoint = "ApplyInteractionStamps";
			sci.FilePath = "InteractionFieldUpdate.hlsl";

			RefCntAutoPtr<IShader> pCS;
			ctx.pDevice->CreateShader(sci, &pCS);
			ASSERT(pCS, "CreateShader(InteractionApplyStampsCS) failed.");

			ComputePipelineStateCreateInfo psoCI = {};
			psoCI.PSODesc.Name = "PSO_InteractionApplyStamps";
			psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

			auto& rl = psoCI.PSODesc.ResourceLayout;
			rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			ShaderResourceVariableDesc vars[] =
			{
				{ SHADER_TYPE_COMPUTE, "g_RWInteractionField",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "g_Stamps",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				{ SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

			psoCI.pCS = pCS;

			ctx.pDevice->CreateComputePipelineState(psoCI, &m_pInteractionApplyCSO);
			ASSERT(m_pInteractionApplyCSO, "CreateComputePipelineState(PSO_InteractionApplyStamps) failed.");

			m_pInteractionApplyCSO->CreateShaderResourceBinding(&m_pInteractionApplySRB, true);
			ASSERT(m_pInteractionApplySRB, "Create SRB for InteractionApplyStamps failed.");

			if (auto* var = m_pInteractionApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INTERACTION_CONSTANTS"))
				var->Set(m_pInteractionConstantsCB);

			if (auto* var = m_pInteractionApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Stamps"))
				var->Set(m_pInteractionStampBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
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

		m_pInteractionDecaySRB.Release();
		m_pInteractionDecayCSO.Release();

		m_pInteractionApplySRB.Release();
		m_pInteractionApplyCSO.Release();

		m_pInteractionConstantsCB.Release();
		m_pInteractionStampBuffer.Release();

		m_pInteractionFieldTex.Release();
		m_pInteractionFieldSRV = nullptr;
		m_pInteractionFieldUAV = nullptr;

		m_pGrassInstanceBuffer.Release();
		m_pIndirectArgsBuffer.Release();
		m_pCounterBuffer.Release();

		m_pGrassGenConstantsCB.Release();
		m_pGrassRenderConstantsCB.Release();
	}

	void GrassRenderPass::BeginFrame(RenderPassContext& ctx)
	{
		buildFramebufferForCurrentBackBuffer(ctx);
	}

	void GrassRenderPass::Execute(RenderPassContext& ctx)
	{
		ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
		ASSERT(m_pRenderPass, "Grass RenderPass is null.");
		ASSERT(m_pFramebuffer, "Grass Framebuffer is null.");

		if (!ctx.pHeightMap || !ctx.pHeightMap->Texture)
			return;

		if (!m_pGrassMesh)
			return;

		if (!m_pGrassDensityFieldTex || !m_pGrassDensityFieldTex->Texture)
			return;

		IDeviceContext* pContext = ctx.pImmediateContext;

		// ---------------------------------------------------------------------
		// (0) Reset counter + init indirect args
		// ---------------------------------------------------------------------
		{
			const uint32 zero = 0;
			pContext->UpdateBuffer(
				m_pCounterBuffer,
				0,
				sizeof(uint32),
				&zero,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			const uint32 args[5] = { 6u, 0u, 0u, 0u, 0u };
			pContext->UpdateBuffer(
				m_pIndirectArgsBuffer,
				0,
				sizeof(args),
				args,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		}

		// ---------------------------------------------------------------------
		// (A) Update GrassGenConstants (also used for stamp world->uv mapping)
		// ---------------------------------------------------------------------
		hlsl::GrassGenConstants gen = {};
		{
			MapHelper<hlsl::GrassGenConstants> map(
				pContext,
				m_pGrassGenConstantsCB,
				MAP_WRITE,
				MAP_FLAG_DISCARD);

			// --- Terrain / Height decode ---
			map->HeightScale = 100.0f;
			map->HeightOffset = 0.0f;
			map->YOffset = 0.0f;
			map->_padT0 = 0.0f;

			map->HFWidth = 1025;
			map->HFHeight = 1025;
			map->CenterXZ = 1;
			map->_padT1 = 0;

			map->SpacingX = 1.0f;
			map->SpacingZ = 1.0f;
			map->_padT2 = 0.0f;
			map->_padT3 = 0.0f;

			// --- Chunk placement ---
			map->ChunkSize = 4.0f;
			map->ChunkHalfExtent = 32;
			map->SamplesPerChunk = 2048;
			map->Jitter = 0.95f;

			map->MinScale = 5.7f;
			map->MaxScale = 11.1f;
			map->SpawnProb = 0.75f;
			map->SpawnRadius = 1000.0f;

			map->BendStrengthMin = 0.95f;
			map->BendStrengthMax = 1.55f;
			map->SeedSalt = 0xA53A9E37u;
			map->_padT4 = 0;

			map->DensityTiling = 0.02f;
			map->DensityContrast = 0.28f;
			map->DensityPow = 0.70f;
			map->_padD0 = 0.0f;

			map->SlopeToDensity = 0.15f;

			map->HeightMinN = 0.00f;
			map->HeightMaxN = 1.00f;
			map->HeightFadeN = 0.03f;

			gen = *map;
		}

		// ---------------------------------------------------------------------
		// (B) GrassRenderConstants
		// ---------------------------------------------------------------------
		{
			MapHelper<hlsl::GrassRenderConstants> map(
				pContext,
				m_pGrassRenderConstantsCB,
				MAP_WRITE,
				MAP_FLAG_DISCARD);

			map->BaseColorFactor = float4(150.f, 200.f, 100.f, 255.f) / 255.f;
			map->Tint = float4{ 1.05f, 1.00f, 0.95f, 1.0f };

			map->AlphaCut = 0.5f;

			map->Ambient = 0.30f;
			map->ShadowStregth = 0.18f;
			map->DirectLightStrength = 0.22f;

			map->WindDirXZ = NormalizeSafe(float2{ 0.80f, 0.60f });
			map->WindStrength = 1.15f;
			map->WindSpeed = 1.75f;

			map->WindFreq = 0.155f;
			map->WindGust = 0.42f;
			map->MaxBendAngle = 1.50f;
			map->_pad1 = 0.0f;

			// Interaction bending (defaults)
			map->InteractionBendAngle = 1.0f;
			map->InteractionSink = 0.05f;
			map->InteractionWindFade = 0.95f;
		}

		// ---------------------------------------------------------------------
		// (C) Upload stamps + InteractionConstants (stamps come from ctx.InteractionStamps)
		// - IMPORTANT: HLSL Apply assumes stamps in TERRAIN UV space (0..1)
		// ---------------------------------------------------------------------
		uint32 stampCount = 0;
		{
			// Upload stamps (convert world->uv)
			MapHelper<hlsl::InteractionStamp> stampMap(
				pContext,
				m_pInteractionStampBuffer,
				MAP_WRITE,
				MAP_FLAG_DISCARD);

			stampCount = 0;

			const auto& stamps = ctx.InteractionStamps; // user said: can obtain via ctx.InteractionStamps
			if (!stamps.empty())
			{
				stampCount = (uint32)std::min<size_t>(stamps.size(), MAX_NUM_INTERACTION_STAMPS);

				for (uint32 i = 0; i < stampCount; ++i)
				{
					hlsl::InteractionStamp s = stamps[i];

					// Convert world XZ -> terrain uv.
					// If your stamps are already uv, remove this conversion.
					s.CenterXZ = WorldXZToTerrainUV(gen, s.CenterXZ);
					s.Radius = WorldRadiusToUv_MinAxis(gen, s.Radius);

					stampMap[i] = s;
				}
			}
		}

		{
			MapHelper<hlsl::InteractionConstants> map(
				pContext,
				m_pInteractionConstantsCB,
				MAP_WRITE,
				MAP_FLAG_DISCARD);

			map->FieldWidth = m_InteractionW;
			map->FieldHeight = m_InteractionH;
			map->NumStamps = stampCount;
			map->DeltaTime = ctx.DeltaTime;

			map->DecayPerSec = 0.15f;
			map->ClampMax = 1.0f;
			map->ClampMin = 0.0f;
			map->_Pad0 = 0.0f;
		}

		// ---------------------------------------------------------------------
		// (D) Interaction update: Decay -> ApplyStamps
		// ---------------------------------------------------------------------
		{
			// Transition interaction field to UAV
			StateTransitionDesc tr[] =
			{
				{ m_pInteractionFieldTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(tr), tr);

			// Decay
			if (auto* var = m_pInteractionDecaySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
				var->Set(m_pInteractionFieldUAV);

			pContext->SetPipelineState(m_pInteractionDecayCSO);
			pContext->CommitShaderResources(m_pInteractionDecaySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			DispatchComputeAttribs disp = {};
			disp.ThreadGroupCountX = DivUp(m_InteractionW, 8);
			disp.ThreadGroupCountY = DivUp(m_InteractionH, 8);
			disp.ThreadGroupCountZ = 1;
			pContext->DispatchCompute(disp);

			// Apply stamps (optional)
			if (stampCount > 0)
			{
				if (auto* var = m_pInteractionApplySRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_RWInteractionField"))
					var->Set(m_pInteractionFieldUAV);

				pContext->SetPipelineState(m_pInteractionApplyCSO);
				pContext->CommitShaderResources(m_pInteractionApplySRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				pContext->DispatchCompute(disp);
			}

			// Transition to SRV for sampling in GenCS
			StateTransitionDesc trSrv[] =
			{
				{ m_pInteractionFieldTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(trSrv), trSrv);
		}

		// ---------------------------------------------------------------------
		// (1) Compute: GenerateGrassInstances
		// ---------------------------------------------------------------------
		{
			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightMap"))
				var->Set(ctx.pHeightMap->Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
				var->Set(m_pGrassDensityFieldTex->Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

			if (auto* var = m_pGenCSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
				var->Set(m_pInteractionFieldSRV);

			StateTransitionDesc tr[] =
			{
				{ m_pGrassInstanceBuffer,           RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pCounterBuffer,                 RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ ctx.pHeightMap->Texture,          RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,  STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGrassDensityFieldTex->Texture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,  STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pInteractionFieldTex,           RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,  STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pContext->TransitionResourceStates(_countof(tr), tr);

			pContext->SetPipelineState(m_pGenCSO);
			pContext->CommitShaderResources(m_pGenCSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

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

		// ---------------------------------------------------------------------
		// (3) Graphics transitions (OUTSIDE render pass)
		// ---------------------------------------------------------------------
		{
			StateTransitionDesc trGfx[] =
			{
				{ m_pGrassInstanceBuffer,        RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,   STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pIndirectArgsBuffer,         RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDIRECT_ARGUMENT, STATE_TRANSITION_FLAG_UPDATE_STATE },

				{ m_pGrassMesh->VertexBuffer,    RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER,     STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_pGrassMesh->IndexBuffer,     RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER,      STATE_TRANSITION_FLAG_UPDATE_STATE },
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

			// VB/IB
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
				MapHelper<hlsl::DrawConstants> map(pContext, ctx.pRegistry->GetBuffer(kRes_DrawCB), MAP_WRITE, MAP_FLAG_DISCARD);
				map->StartInstanceLocation = 0;
			}

			DrawIndexedIndirectAttribs ia = {};
			ia.IndexType = m_pGrassMesh->IndexType;
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
		(void)ctx;
		(void)width;
		(void)height;

		m_pFramebuffer.Release();
	}

	void GrassRenderPass::SetGrassModel(RenderPassContext& ctx, const StaticMeshRenderData& mesh)
	{
		(void)ctx;
		ASSERT(m_pGrassPSO, "Grass render pass is not initialied yet.");
		m_pGrassMesh = &mesh;
	}

	void GrassRenderPass::SetGrassDensityField(RenderPassContext& ctx, const TextureRenderData& tex)
	{
		(void)ctx;
		ASSERT(m_pGrassPSO, "Grass render pass is not initialied yet.");
		m_pGrassDensityFieldTex = &tex;
	}

	bool GrassRenderPass::buildFramebufferForCurrentBackBuffer(RenderPassContext& ctx)
	{
		ASSERT(ctx.pDevice, "Device is null.");
		ASSERT(ctx.pSwapChain, "SwapChain is null.");
		ASSERT(m_pRenderPass, "Grass render pass is null.");

		ITextureView* pRTV = ctx.pRegistry->GetTextureRTV(STRING_HASH("Lighting"));
		ITextureView* pDSV = ctx.pRegistry->GetTextureDSV(STRING_HASH("GBufferDepth"));
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
