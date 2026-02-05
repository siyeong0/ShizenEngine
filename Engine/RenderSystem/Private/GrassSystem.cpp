#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

#include "Engine/AssetManager/Public/AssetManager.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"

#include "Engine/RenderSystem/Public/IndirectArgsSystem.h"
#include "Engine/Framework/Public/TerrainSystem.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	// ---------------------------------------------------------------------
	// InstallPasses
	// ---------------------------------------------------------------------
	void GrassSystem::InstallPasses(Renderer& renderer, IndirectArgsSystem& indirect, TerrainSystem& terrain)
	{
		// Allocate indirect slot for grass
		m_IndirectSlot = indirect.AllocateSlot("GrassSystem");

		// Register template for grass slot
		{
			hlsl::IndirectArgsTemplate t = {};
			// t.IndexCountPerInstance = m_GrassDesc.pMeshLod0->IndexCount;
			t.IndexCountPerInstance = m_GrassDesc.pCrossMeshLod1->IndexCount; // Cross
			// t.IndexCountPerInstance = 6; // Billboard
			t.StartIndexLocation = 0;
			t.BaseVertexLocation = 0;
			t.StartInstanceLocation = 0;

			indirect.SetTemplate(m_IndirectSlot, t);
		}

		// =====================================================================
		// Pass 1) GrassGenerateInstances (compute)
		// =====================================================================
		renderer.AddPass(
			"GrassGenerateInstances",
			[&](RenderPassBuilder& b)
			{
				// Outputs
				b.DeclareBufferUAV(STRING_HASH("GrassInstanceBuffer"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("IndirectCountBuffer"), RENDER_ACCESS_WRITE); // <-- shared counts

				// Inputs
				b.DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));
				b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));

				// Constants
				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstantsCB"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pScene, "Scene is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGenCSO && m_pGenSRB, "GrassGenerate PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// (0) Reset counter for my slot (slot * 4 bytes)
				{
					const uint32 zero = 0;
					const uint32 offset = m_IndirectSlot * 4u;

					pContext->UpdateBuffer(
						ctx.pRegistry->GetBuffer(STRING_HASH("IndirectCountBuffer")),
						offset,
						sizeof(uint32),
						&zero,
						RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

					StateTransitionDesc trBack[] =
					{
						{
							ctx.pRegistry->GetBuffer(STRING_HASH("IndirectCountBuffer")),
							RESOURCE_STATE_UNKNOWN,
							RESOURCE_STATE_UNORDERED_ACCESS,
							STATE_TRANSITION_FLAG_UPDATE_STATE
						},
					};
					pContext->TransitionResourceStates(_countof(trBack), trBack);
				}

				// (1) Bind per-frame textures
				{
					if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightField"))
					{
						var->Set(renderer.GetTextureSRV(STRING_HASH("HeightField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
					if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
					{
						var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("GrassDensityField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
					if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
					{
						var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}

					StateTransitionDesc tr =
					{
						renderer.GetTexture(STRING_HASH("HeightField")),
						RESOURCE_STATE_UNKNOWN,
						RESOURCE_STATE_SHADER_RESOURCE,
						STATE_TRANSITION_FLAG_UPDATE_STATE
					};
					pContext->TransitionResourceStates(1, &tr);
				}

				// (2) Dispatch
				{
					pContext->SetPipelineState(m_pGenCSO);
					pContext->CommitShaderResources(m_pGenSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

					DispatchComputeAttribs disp = {};
					disp.ThreadGroupCountX = (2u * 32u + 8u - 1u) / 8u;
					disp.ThreadGroupCountY = (2u * 32u + 8u - 1u) / 8u;
					disp.ThreadGroupCountZ = 1;

					pContext->DispatchCompute(disp);
				}
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "GenerateGrassInstances";
				csCI.Desc.Name = "GrassGenerateInstancesCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "GrassGenerateInstancesCS compile failed.");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_GrassGenerateInstances";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_OutInstances",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_Counter",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_DensityField",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_InteractionField",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				SamplerDesc linearWrap =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
				};
				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_COMPUTE, "g_LinearWrapSampler", linearWrap },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pGenCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pGenCSO, "AcquireCompute(GrassGenerateInstances) failed.");

				m_pGenCSO->CreateShaderResourceBinding(&m_pGenSRB, true);
				ASSERT(m_pGenSRB, "GrassGenerateInstances SRB create failed.");

				// Bind stable resources (buffers / CB)
				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstances"))
				{
					var->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBuffer")));
				}
				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Counter"))
				{
					var->Set(renderer.GetBufferUAV(STRING_HASH("IndirectCountBuffer")));
				}
				if (auto* var = m_pGenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstantsCB")));
				}
			});
		// =====================================================================
		// Pass 2) CopyLightingToGrassMSAA (graphics fullscreen)
		//   Lighting(1x) -> GrassColorMSAA(4x)
		// =====================================================================
		renderer.AddPass(
			"CopyLightingToGrassMSAA",
			[&](RenderPassBuilder& b)
			{
				b.DeclareTextureRTVWrite(STRING_HASH("GrassColorMSAA"));
				b.DeclareTextureSRVRead(STRING_HASH("LightingScene"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pCopyToMSAAPSO && m_pCopyToMSAASRB, "CopyToMSAA PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// Bind src texture (Lighting SRV)
				if (auto* var = m_pCopyToMSAASRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SrcTex"))
				{
					var->Set(ctx.pRegistry->GetTextureSRV(STRING_HASH("LightingScene")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}

				pContext->SetPipelineState(m_pCopyToMSAAPSO);
				pContext->CommitShaderResources(m_pCopyToMSAASRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				pContext->Draw(da);
			},
				[this, &renderer]()
			{
				GraphicsPipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_CopyLightingToGrassMSAA";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				auto& gp = psoCI.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

				gp.SmplDesc.Count = 4;
				gp.SmplDesc.Quality = 0;

				gp.DepthStencilDesc.DepthEnable = false;
				gp.RasterizerDesc.CullMode = CULL_MODE_NONE;

				ShaderCreateInfo vsCI = {};
				vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				vsCI.EntryPoint = "main";
				vsCI.Desc.Name = "FullscreenTriVS";
				vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
				vsCI.Desc.UseCombinedTextureSamplers = false;
				vsCI.FilePath = m_CopyVS.c_str();

				ShaderCreateInfo psCI = {};
				psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				psCI.EntryPoint = "main";
				psCI.Desc.Name = "CopyTexturePS";
				psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
				psCI.Desc.UseCombinedTextureSamplers = false;
				psCI.FilePath = m_CopyPS.c_str();

				renderer.CreateShader(vsCI, &psoCI.pVS);
				renderer.CreateShader(psCI, &psoCI.pPS);
				ASSERT(psoCI.pVS && psoCI.pPS, "Copy shaders compile failed.");

				psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_SrcTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				psoCI.PSODesc.ResourceLayout.Variables = vars;
				psoCI.PSODesc.ResourceLayout.NumVariables = _countof(vars);

				SamplerDesc linearClamp =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
				};
				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
				};
				psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pCopyToMSAAPSO = renderer.AcquirePipelineState(STRING_HASH("CopyLightingToGrassMSAA"), psoCI);
				ASSERT(m_pCopyToMSAAPSO, "AcquirePipelineState(CopyLightingToGrassMSAA) failed.");

				m_pCopyToMSAAPSO->CreateShaderResourceBinding(&m_pCopyToMSAASRB, true);
				ASSERT(m_pCopyToMSAASRB, "CopyToMSAA SRB create failed.");
			});
		// =====================================================================
		// Pass 3) GrassForwardMSAA (graphics, DrawIndexedIndirect)
		//   Draw into GrassColorMSAA(4x) + GrassDepthMSAA(4x)
		//   Read scene depth from GBufferDepth as SRV (manual occlusion)
		// =====================================================================
		renderer.AddPass(
			"GrassForwardMSAA",
			[&](RenderPassBuilder& b)
			{
				// MSAA targets
				b.DeclareTextureRTVReadWrite(STRING_HASH("GrassColorMSAA"));
				b.DeclareTextureDSVWrite(STRING_HASH("GrassDepthMSAA"));

				// Inputs
				b.DeclareTextureSRVRead(STRING_HASH("ShadowMap"));
				b.DeclareTextureSRVRead(STRING_HASH("GBufferDepth")); // Scene depth SRV (R32_FLOAT)

				b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBuffer"));
				b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectArgsBuffer"));
				b.DeclareBufferCBVRead(STRING_HASH("GrassRenderConstantsCB"));

				b.SetClearDepthStencil(STRING_HASH("GrassDepthMSAA"), 1.f, 0);
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGrassPSO && m_pGrassSRB, "Grass PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				//pContext->SetPipelineState(m_pGrassPSO);
				//pContext->CommitShaderResources(m_pGrassSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				//IBuffer* ppVertexBuffers[] = { m_GrassDesc.pMeshLod0->VertexBuffer };
				//uint64 offsets[] = { 0 };

				//pContext->SetVertexBuffers(
				//	0, 1, ppVertexBuffers, offsets,
				//	RESOURCE_STATE_TRANSITION_MODE_VERIFY,
				//	SET_VERTEX_BUFFERS_FLAG_RESET);

				//pContext->SetIndexBuffer(
				//	m_GrassDesc.pMeshLod0->IndexBuffer, 0,
				//	RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				//DrawIndexedIndirectAttribs dia = {};
				//dia.IndexType = m_GrassDesc.pMeshLod0->IndexType;

				//dia.DrawArgsOffset = m_IndirectSlot * 20;
				//dia.DrawCount = 1;
				//dia.DrawArgsStride = 20;

				//dia.pAttribsBuffer = renderer.GetBuffer(STRING_HASH("IndirectArgsBuffer"));
				//dia.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

				//pContext->DrawIndexedIndirect(dia);

				pContext->SetPipelineState(m_pGrassCrossPSO);
				pContext->CommitShaderResources(m_pGrassCrossSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				IBuffer* ppVertexBuffers[] = { m_GrassDesc.pCrossMeshLod1->VertexBuffer };
				uint64 offsets[] = { 0 };

				pContext->SetVertexBuffers(
					0, 1, ppVertexBuffers, offsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pContext->SetIndexBuffer(
					m_GrassDesc.pCrossMeshLod1->IndexBuffer, 0,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawIndexedIndirectAttribs dia = {};
				dia.IndexType = m_GrassDesc.pCrossMeshLod1->IndexType;

				dia.DrawArgsOffset = m_IndirectSlot * 20;
				dia.DrawCount = 1;
				dia.DrawArgsStride = 20;

				dia.pAttribsBuffer = renderer.GetBuffer(STRING_HASH("IndirectArgsBuffer"));
				dia.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

				pContext->DrawIndexedIndirect(dia);

				//pContext->SetPipelineState(m_pGrassBillboardPSO);
				//pContext->CommitShaderResources(m_pGrassBillboardSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				//IBuffer* ppVertexBuffers[] = { m_GrassDesc.pBillboardMeshLod2->VertexBuffer };
				//uint64 offsets[] = { 0 };

				//pContext->SetVertexBuffers(
				//	0, 1, ppVertexBuffers, offsets,
				//	RESOURCE_STATE_TRANSITION_MODE_VERIFY,
				//	SET_VERTEX_BUFFERS_FLAG_RESET);

				//pContext->SetIndexBuffer(
				//	m_GrassDesc.pBillboardMeshLod2->IndexBuffer, 0,
				//	RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				//DrawIndexedIndirectAttribs dia = {};
				//dia.IndexType = VT_UINT16;

				//dia.DrawArgsOffset = m_IndirectSlot * 20;
				//dia.DrawCount = 1;
				//dia.DrawArgsStride = 20;

				//dia.pAttribsBuffer = renderer.GetBuffer(STRING_HASH("IndirectArgsBuffer"));
				//dia.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

				//pContext->DrawIndexedIndirect(dia);
			},
				[this, &renderer]()
			{
				// Grass mesh
				{
					GraphicsPipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "PSO_GrassMSAA_A2C";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

					auto& gp = psoCI.GraphicsPipeline;
					gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

					gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
					gp.RasterizerDesc.FrontCounterClockwise = true;

					// MSAA must match attachment sample count (4x)
					gp.SmplDesc.Count = 4;
					gp.SmplDesc.Quality = 0;

					// Fixed depth for grass self-occlusion
					gp.DepthStencilDesc.DepthEnable = true;
					gp.DepthStencilDesc.DepthWriteEnable = true;
					gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

					// A2C ON
					gp.BlendDesc.AlphaToCoverageEnable = true;

					// Shaders
					ShaderCreateInfo vsCI = {};
					vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					vsCI.EntryPoint = "main";
					vsCI.Desc.Name = "GrassVS";
					vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
					vsCI.Desc.UseCombinedTextureSamplers = false;
					vsCI.FilePath = m_GrassVS.c_str();

					ShaderCreateInfo psCI = {};
					psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					psCI.EntryPoint = "main";
					psCI.Desc.Name = "GrassPS";
					psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
					psCI.Desc.UseCombinedTextureSamplers = false;
					psCI.FilePath = m_GrassPS.c_str();

					renderer.CreateShader(vsCI, &psoCI.pVS);
					renderer.CreateShader(psCI, &psoCI.pPS);
					ASSERT(psoCI.pVS && psoCI.pPS, "Grass VS/PS compile failed.");

					psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						{ SHADER_TYPE_PIXEL, "g_BaseColorTex",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};
					psoCI.PSODesc.ResourceLayout.Variables = vars;
					psoCI.PSODesc.ResourceLayout.NumVariables = _countof(vars);

					SamplerDesc linearWrap =
					{
						FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
						TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
					};
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
						{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler",  linearWrap },
						{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
						{ SHADER_TYPE_PIXEL, "g_PointClampSampler",  pointClamp },
						{ SHADER_TYPE_PIXEL, "g_ShadowCmpSampler",   shadowClamp },
					};
					psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
					psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

					static LayoutElement layoutElems[] =
					{
						LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
						LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
						LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
						LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
					};
					gp.InputLayout.LayoutElements = layoutElems;
					gp.InputLayout.NumElements = _countof(layoutElems);

					m_pGrassPSO = renderer.AcquirePipelineState(STRING_HASH("GrassForwardMSAA"), psoCI, true);
					ASSERT(m_pGrassPSO, "AcquirePipelineState(GrassForwardMSAA) failed.");

					// Static bindings
					m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances")->Set(renderer.GetBufferSRV(STRING_HASH("GrassInstanceBuffer")));
					m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS")->Set(renderer.GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
					m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "GRASS_RENDER_CONSTANTS")->Set(renderer.GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
					m_pGrassPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_SceneDepth")->Set(renderer.GetTextureSRV(STRING_HASH("GBufferDepth")));

					m_pGrassSRB = renderer.AcquireShaderResourceBindingFromMaterial(m_GrassDesc.pMeshLod0->Sections[0].MaterialId, m_pGrassPSO);
					ASSERT(m_pGrassSRB, "Grass SRB create failed.");
				}

				// Grass cross-plane
				{
					m_pGrassCrossPSO = m_pGrassPSO;
					m_pGrassCrossSRB = renderer.AcquireShaderResourceBindingFromMaterial(m_GrassDesc.pCrossMeshLod1->Sections[0].MaterialId, m_pGrassPSO);
				}

				// Grass billboard 
				{
					GraphicsPipelineStateCreateInfo psoCI = {};
					psoCI.PSODesc.Name = "PSO_GrassBillboardMSAA_A2C";
					psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

					auto& gp = psoCI.GraphicsPipeline;
					gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

					gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
					gp.RasterizerDesc.FrontCounterClockwise = true;

					// MSAA must match attachment sample count (4x)
					gp.SmplDesc.Count = 4;
					gp.SmplDesc.Quality = 0;

					// Fixed depth for grass self-occlusion
					gp.DepthStencilDesc.DepthEnable = true;
					gp.DepthStencilDesc.DepthWriteEnable = true;
					gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

					// A2C ON
					gp.BlendDesc.AlphaToCoverageEnable = true;

					// Shaders
					ShaderCreateInfo vsCI = {};
					vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					vsCI.EntryPoint = "main";
					vsCI.Desc.Name = "GrassBillboardVS";
					vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
					vsCI.Desc.UseCombinedTextureSamplers = false;
					vsCI.FilePath = m_GrassBillboardVS.c_str();

					ShaderCreateInfo psCI = {};
					psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					psCI.EntryPoint = "main";
					psCI.Desc.Name = "GrassBillbiardPS";
					psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
					psCI.Desc.UseCombinedTextureSamplers = false;
					psCI.FilePath = m_GrassBillboardPS.c_str();

					renderer.CreateShader(vsCI, &psoCI.pVS);
					renderer.CreateShader(psCI, &psoCI.pPS);
					ASSERT(psoCI.pVS && psoCI.pPS, "Grass VS/PS compile failed.");

					psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					SamplerDesc linearWrap =
					{
						FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
						TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
					};
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
						{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler",  linearWrap },
						{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
						{ SHADER_TYPE_PIXEL, "g_PointClampSampler",  pointClamp },
						{ SHADER_TYPE_PIXEL, "g_ShadowCmpSampler",   shadowClamp },
					};
					psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
					psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

					static LayoutElement layoutElems[] =
					{
						LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
						LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
					};
					gp.InputLayout.LayoutElements = layoutElems;
					gp.InputLayout.NumElements = _countof(layoutElems);

					m_pGrassBillboardPSO = renderer.AcquirePipelineState(STRING_HASH("GrassForwardMSAA"), psoCI);
					ASSERT(m_pGrassBillboardPSO, "AcquirePipelineState(GrassForwardMSAA) failed.");

					// Static bindings
					if (auto* pVar = m_pGrassBillboardPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances"))
					{
						pVar->Set(renderer.GetBufferSRV(STRING_HASH("GrassInstanceBuffer")));
					}
					if (auto* pVar = m_pGrassBillboardPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_SceneDepth"))
					{
						pVar->Set(renderer.GetTextureSRV(STRING_HASH("GBufferDepth")));
					}
					if (auto* pVar = m_pGrassBillboardPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
					{
						pVar->Set(m_GrassDesc.pBillboardMeshLod2->BaseColorTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
					}

					m_pGrassBillboardPSO->CreateShaderResourceBinding(&m_pGrassBillboardSRB, true);
					ASSERT(m_pGrassBillboardSRB, "Grass SRB create failed.");
				}
			});

		// =====================================================================
		// Pass 4) ResolveGrassToLighting
		//   Resolve GrassColorMSAA(4x) -> Lighting(1x)
		// =====================================================================
		renderer.AddPass(
			"ResolveGrassToLighting",
			[&](RenderPassBuilder& b)
			{
				b.DeclareTextureRTVWrite(STRING_HASH("LightingFinal"));
				b.DeclareTextureSRVRead(STRING_HASH("GrassColorMSAA"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				ITexture* pSrc = ctx.pRegistry->GetTexture(STRING_HASH("GrassColorMSAA"));
				ITexture* pDst = ctx.pRegistry->GetTexture(STRING_HASH("LightingFinal"));
				ASSERT(pSrc && pDst, "Resolve textures are null.");

				ResolveTextureSubresourceAttribs r = {};
				r.SrcMipLevel = 0;
				r.DstMipLevel = 0;
				r.SrcSlice = 0;
				r.DstSlice = 0;

				pContext->ResolveTextureSubresource(pSrc, pDst, r);
			}, {}, EPassExecutionDomain::OutsideRenderPass);

		// =====================================================================
		// Pass X) GrassShadow (graphics, DrawIndexedIndirect into ShadowMap)
		// =====================================================================
		renderer.AddPass(
			"GrassShadow",
			[&](RenderPassBuilder& b)
			{
				b.DeclareTextureDSVWrite(STRING_HASH("ShadowMap"));

				b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBuffer"));

				b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectArgsBuffer"));

				b.DeclareBufferSRVRead(STRING_HASH("DEP00"));

				b.DeclareBufferCBVRead(STRING_HASH("GrassRenderConstantsCB"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pGrassShadowPSO && m_pGrassShadowSRB, "GrassShadow PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// viewport to shadow map resolution
				Viewport vp = {};
				vp.Width = float(ctx.ShadowMapResolution);
				vp.Height = float(ctx.ShadowMapResolution);
				vp.MinDepth = 0.f;
				vp.MaxDepth = 1.f;
				pContext->SetViewports(1, &vp, 0, 0);

				pContext->SetPipelineState(m_pGrassShadowPSO);
				pContext->CommitShaderResources(m_pGrassShadowSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				IBuffer* ppVertexBuffers[] = { m_GrassDesc.pMeshLod0->VertexBuffer };
				uint64 offsets[] = { 0 };

				pContext->SetVertexBuffers(
					0,
					1,
					ppVertexBuffers,
					offsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				pContext->SetIndexBuffer(
					m_GrassDesc.pMeshLod0->IndexBuffer,
					0,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				DrawIndexedIndirectAttribs dia;
				dia.IndexType = m_GrassDesc.pMeshLod0->IndexType;

				dia.DrawArgsOffset = m_IndirectSlot * 20u;
				dia.DrawCount = 1;
				dia.DrawArgsStride = 20;

				dia.pAttribsBuffer = renderer.GetBuffer(STRING_HASH("IndirectArgsBuffer"));
				dia.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

				dia.pCounterBuffer = nullptr;
				dia.CounterOffset = 0;
				dia.CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

				pContext->DrawIndexedIndirect(dia);
			},
				[this, &renderer]()
			{
				// ------------------------------------------------------------
				// Build PSO for GrassShadow
				//  - VS: GrassShadow.vsh
				//  - PS: Shadow.psh (depth-only)
				// ------------------------------------------------------------
				GraphicsPipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_GrassShadow";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

				auto& gp = psoCI.GraphicsPipeline;
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

				// Depth-only target: RTV 0, DSV is from render pass
				gp.NumRenderTargets = 0;
				gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
				gp.DSVFormat = TEX_FORMAT_UNKNOWN;

				gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
				gp.RasterizerDesc.FrontCounterClockwise = true;

				gp.DepthStencilDesc.DepthEnable = true;
				gp.DepthStencilDesc.DepthWriteEnable = true;
				gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

				static LayoutElement layoutElems[] =
				{
					LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
					LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
					LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
					LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
				};
				gp.InputLayout.LayoutElements = layoutElems;
				gp.InputLayout.NumElements = _countof(layoutElems);

				// Shaders
				{
					ShaderCreateInfo vsCI = {};
					vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					vsCI.EntryPoint = "main";
					vsCI.Desc.Name = "GrassShadow VS";
					vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
					vsCI.Desc.UseCombinedTextureSamplers = false;
					vsCI.FilePath = m_GrassShadowVS.c_str();

					ShaderCreateInfo psCI = {};
					psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					psCI.EntryPoint = "main";
					psCI.Desc.Name = "GrassShadow PS(Shadow.psh)";
					psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
					psCI.Desc.UseCombinedTextureSamplers = false;
					psCI.FilePath = m_ShadowPS.c_str();

					renderer.CreateShader(vsCI, &psoCI.pVS);
					renderer.CreateShader(psCI, &psoCI.pPS);
					ASSERT(psoCI.pVS && psoCI.pPS, "GrassShadow VS/PS compile failed.");
				}

				// Resource layout
				psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_PIXEL, "g_BaseColorTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_PIXEL, "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				psoCI.PSODesc.ResourceLayout.Variables = vars;
				psoCI.PSODesc.ResourceLayout.NumVariables = _countof(vars);

				SamplerDesc linearWrap =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
				};

				ImmutableSamplerDesc samplers[] =
				{
					{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrap },
				};
				psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
				psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

				m_pGrassShadowPSO = renderer.AcquirePipelineState(STRING_HASH("GrassShadow"), psoCI);
				ASSERT(m_pGrassShadowPSO, "AcquirePipelineState(GrassShadow) failed.");

				// ---- Bind static resources ----
				// Grass instances
				if (auto* var = m_pGrassShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_GrassInstances"))
				{
					var->Set(renderer.GetBufferSRV(STRING_HASH("GrassInstanceBuffer")));
				}

				// Shadow constants 
				if (auto* var = m_pGrassShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("SHADOW_CONSTANTS")));
				}

				// Grass render constants
				if (auto* var = m_pGrassShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "GRASS_RENDER_CONSTANTS"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("GrassRenderConstantsCB")));
				}
				m_pGrassShadowSRB = renderer.AcquireShaderResourceBindingFromMaterial(m_GrassDesc.pMeshLod0->Sections[0].MaterialId, m_pGrassShadowPSO);
				ASSERT(m_pGrassShadowSRB, "GrassShadow SRB create failed.");
			});
	}
} // namespace shz
