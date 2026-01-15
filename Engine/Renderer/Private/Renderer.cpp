#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		ASSERT(createInfo.pDevice, "Device is null.");
		ASSERT(createInfo.pImmediateContext, "ImmediateContext is null.");
		ASSERT(createInfo.pSwapChain, "SwapChain is null.");
		ASSERT(createInfo.pAssetManager, "AssetManager is null.");

		m_CreateInfo = createInfo;
		m_pAssetManager = m_CreateInfo.pAssetManager;

		const SwapChainDesc& swapChainDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : swapChainDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : swapChainDesc.Height;

		// Shader source factory
		m_CreateInfo.pEngineFactory->CreateDefaultShaderSourceStreamFactory(m_CreateInfo.ShaderRootDir,&m_pShaderSourceFactory);

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Frame constants CB", &m_pFrameCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ObjectConstants), "Object constants CB", &m_pObjectCB);

		if (!CreateBasicPSO())
		{
			ASSERT(false, "Failed to create BasicPSO.");
			return false;
		}

		// Default sampler
		{
			SamplerDesc samplerDesc = {};
			samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
			samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
			samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
			samplerDesc.AddressU = TEXTURE_ADDRESS_WRAP;
			samplerDesc.AddressV = TEXTURE_ADDRESS_WRAP;
			samplerDesc.AddressW = TEXTURE_ADDRESS_WRAP;

			m_CreateInfo.pDevice->CreateSampler(samplerDesc, &m_pDefaultSampler);
			ASSERT(m_pDefaultSampler, "Failed to create default sampler.");
		}

		// Create resource cache (owned)
		{
			ASSERT(m_pRenderResourceCache, "Resource cache already created.");

			m_pRenderResourceCache = std::make_unique<RenderResourceCache>();

			RenderResourceCacheCreateInfo RCI = {};
			RCI.pDevice = m_CreateInfo.pDevice;
			RCI.pAssetManager = m_pAssetManager;
			RCI.pDefaultSampler = m_pDefaultSampler;

			if (!m_pRenderResourceCache->Initialize(RCI))
			{
				ASSERT(false, "Failed to initialzie RenderResourceCache.");
				return false;
			}
		}

		return true;
	}

	void Renderer::Cleanup()
	{
		m_pBasicSRB.Release();
		m_pBasicPSO.Release();

		m_pFrameCB.Release();
		m_pObjectCB.Release();

		m_pShaderSourceFactory.Release();
		m_pDefaultSampler.Release();

		m_pAssetManager = nullptr;
		m_CreateInfo = {};
		m_Width = 0;
		m_Height = 0;
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		m_Width = width;
		m_Height = height;
	}

	void Renderer::BeginFrame() {}
	void Renderer::EndFrame() {}

	// ============================================================
	// Resource forwarding API
	// ============================================================

	Handle<StaticMeshRenderData> Renderer::CreateStaticMesh(Handle<StaticMeshAsset> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->CreateStaticMesh(h);
	}

	Handle<StaticMeshRenderData> Renderer::CreateCubeMesh()
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->CreateCubeMesh();
	}

	bool Renderer::DestroyStaticMesh(Handle<StaticMeshRenderData> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->DestroyStaticMesh(h);
	}

	bool Renderer::DestroyMaterialInstance(Handle<MaterialInstance> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->DestroyMaterialInstance(h);
	}

	bool Renderer::DestroyTextureGPU(Handle<ITexture> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->DestroyTextureGPU(h);
	}

	// ============================================================
	// Render
	// ============================================================

	void Renderer::Render(const RenderScene& scene, const ViewFamily& viewFamily)
	{
		IDeviceContext* pImmediateContext = m_CreateInfo.pImmediateContext.RawPtr();
		ISwapChain* pSwapChain = m_CreateInfo.pSwapChain.RawPtr();

		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		ASSERT(pImmediateContext, "ImmediateContext is null.");
		ASSERT(pSwapChain, "ImmediateContext is null.");

		ITextureView* pRTV = pSwapChain->GetCurrentBackBufferRTV();
		ITextureView* pDSV = pSwapChain->GetDepthBufferDSV();
		pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		const float clearColor[] = { 0.350f, 0.350f, 0.350f, 1.0f };
		pImmediateContext->ClearRenderTarget(pRTV, clearColor, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
		pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		if (viewFamily.Views.empty())
		{
			ASSERT(false, "ViewFamily is empty. Please fill the view infos.");
			return;
		}

		const View& view = viewFamily.Views[0];
		Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;

		// Frame CB update
		{
			MapHelper<hlsl::FrameConstants> cbData(pImmediateContext, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);
			cbData->ViewProj = viewProj;
		}

		ASSERT(m_pBasicPSO, "BasicPSO is null. Initialize it.");

		for (const Handle<RenderScene::RenderObject>& hObj : scene.GetObjectHandles())
		{
			const RenderScene::RenderObject* renderObject = scene.TryGetObject(hObj);
			if (!renderObject)
			{
				ASSERT(false, "RenderObject handle is invalid.");
				continue;
			}

			const StaticMeshRenderData* pMesh = m_pRenderResourceCache->TryGetMesh(renderObject->MeshHandle);
			if (!pMesh)
			{
				ASSERT(false, "StaticMesh handle is invalid.");
				continue;
			}

			// Object CB update
			{
				MapHelper<hlsl::ObjectConstants> CBData(pImmediateContext, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
				CBData->World = renderObject->Transform;
			}

			IBuffer* pVBs[] = { pMesh->VertexBuffer };
			uint64 Offsets[] = { 0 };
			pImmediateContext->SetVertexBuffers(0, 1, pVBs, Offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);

			Handle<MaterialInstance> lastMat = {};

			for (const MeshSection& section : pMesh->Sections)
			{
				pImmediateContext->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				const Handle<MaterialInstance> matHandle = section.Material;

				MaterialRenderData* matRD = m_pRenderResourceCache->GetOrCreateMaterialRenderData(matHandle, m_pBasicPSO, m_pFrameCB, m_pObjectCB);
				ASSERT(matRD, "Failed to get or create material render data.");

				if (matHandle != lastMat)
				{
					pImmediateContext->SetPipelineState(matRD->pPSO);
					pImmediateContext->CommitShaderResources(matRD->pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					lastMat = matHandle;
				}

				DrawIndexedAttribs DIA = {};
				DIA.NumIndices = section.NumIndices;
				DIA.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
				DIA.Flags = DRAW_FLAG_VERIFY_ALL;
				DIA.FirstIndexLocation = section.StartIndex;

				pImmediateContext->DrawIndexed(DIA);
			}
		}
	}

	// ============================================================
	// PSO
	// ============================================================

	bool Renderer::CreateBasicPSO()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ASSERT(pDevice, "Device is null.");

		const SwapChainDesc& scDesc = m_CreateInfo.pSwapChain->GetDesc();

		GraphicsPipelineStateCreateInfo psoCreateInfo = {};
		psoCreateInfo.PSODesc.Name = "Debug Basic PSO";
		psoCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCreateInfo.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = scDesc.ColorBufferFormat;
		gp.DSVFormat = scDesc.DepthBufferFormat;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		gp.RasterizerDesc.FrontCounterClockwise = true;
		gp.DepthStencilDesc.DepthEnable = true;

		LayoutElement LlayoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false},
			LayoutElement{1, 0, 2, VT_FLOAT32, false},
			LayoutElement{2, 0, 3, VT_FLOAT32, false},
			LayoutElement{3, 0, 3, VT_FLOAT32, false},
		};
		gp.InputLayout.LayoutElements = LlayoutElems;
		gp.InputLayout.NumElements = _countof(LlayoutElems);

		ShaderCreateInfo shaderCreateInfo = {};
		shaderCreateInfo.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		shaderCreateInfo.EntryPoint = "main";
		shaderCreateInfo.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		shaderCreateInfo.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		RefCntAutoPtr<IShader> pVS;
		{
			shaderCreateInfo.Desc = {};
			shaderCreateInfo.Desc.Name = "Basic VS";
			shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_VERTEX;
			shaderCreateInfo.FilePath = "Basic.vsh";
			shaderCreateInfo.Desc.UseCombinedTextureSamplers = true;

			pDevice->CreateShader(shaderCreateInfo, &pVS);
			if (!pVS)
			{
				ASSERT(false, "Failed to create a basic vertex shader.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> pPS;
		{
			shaderCreateInfo.Desc = {};
			shaderCreateInfo.Desc.Name = "Basic PS";
			shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_PIXEL;
			shaderCreateInfo.FilePath = "Basic.psh";
			shaderCreateInfo.Desc.UseCombinedTextureSamplers = true;

			pDevice->CreateShader(shaderCreateInfo, &pPS);
			if (!pPS)
			{
				ASSERT(false, "Failed to create a basic pixel shader.");
				return false;
			}
		}

		psoCreateInfo.pVS = pVS;
		psoCreateInfo.pPS = pPS;

		psoCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc shaderResVars[] =
		{
			{ SHADER_TYPE_VERTEX, "FRAME_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC  },
			{ SHADER_TYPE_PIXEL,  "g_BaseColorTex",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
		};
		psoCreateInfo.PSODesc.ResourceLayout.Variables = shaderResVars;
		psoCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(shaderResVars);

		pDevice->CreateGraphicsPipelineState(psoCreateInfo, &m_pBasicPSO);
		if (!m_pBasicPSO)
		{
			ASSERT(false, "Failed to create Basic PSO.");
			return false;
		}

		m_pBasicPSO->CreateShaderResourceBinding(&m_pBasicSRB, true);
		if (!m_pBasicSRB)
		{
			ASSERT(false, "Failed to create Basic SRB.");
			return false;
		}

		auto bindCB = [&](const char* name, IBuffer* pCB) -> bool
		{
			if (!pCB) return false;
			if (IShaderResourceVariable* var = m_pBasicSRB->GetVariableByName(SHADER_TYPE_VERTEX, name))
			{
				var->Set(pCB);
				return true;
			}
			return false;
		};

		if (!bindCB("FRAME_CONSTANTS", m_pFrameCB))
		{
			ASSERT(false, "Failed to bind FRAME_CONSTANTS.");
			return false;
		}

		if (!bindCB("OBJECT_CONSTANTS", m_pObjectCB))
		{
			ASSERT(false, "Failed to bind OBJECT_CONSTANTS.");
			return false;
		}

		return true;
	}

} // namespace shz
