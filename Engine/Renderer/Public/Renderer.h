#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

#include "Engine/RHI/Interface/IEngineFactory.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/ISwapChain.h"
#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"

#include "Engine/ImGui/Public/ImGuiImplShizen.hpp"

#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Renderer/Public/MaterialInstance.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/Renderer/Public/RenderResourceCache.h"

namespace shz
{
	class AssetManager;

	struct RendererCreateInfo
	{
		RefCntAutoPtr<IEngineFactory> pEngineFactory;
		RefCntAutoPtr<IRenderDevice> pDevice;
		RefCntAutoPtr<IDeviceContext> pImmediateContext;
		std::vector<RefCntAutoPtr<IDeviceContext>> pDeferredContexts;
		RefCntAutoPtr<ISwapChain> pSwapChain;
		ImGuiImplShizen* pImGui = nullptr;

		// AssetManager reference (not owned)
		AssetManager* pAssetManager = nullptr;

		uint32 BackBufferWidth = 0;
		uint32 BackBufferHeight = 0;

		const char* ShaderRootDir = "C:/Dev/ShizenEngine/Engine/Renderer/Shaders";
	};

	class Renderer final
	{
	public:
		Renderer() = default;
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
		~Renderer() = default;

		bool Initialize(const RendererCreateInfo& createInfo);
		void Cleanup();

		void OnResize(uint32 width, uint32 height);

		void BeginFrame();
		void Render(const RenderScene& scene, const ViewFamily& viewFamily);
		void EndFrame();

		// Resource-facing API (still exposed via Renderer, implemented by RenderResourceCache)
		Handle<StaticMeshRenderData> CreateStaticMesh(Handle<StaticMeshAsset> h);
		Handle<StaticMeshRenderData> CreateCubeMesh();

		bool DestroyStaticMesh(Handle<StaticMeshRenderData> h);
		bool DestroyMaterialInstance(Handle<MaterialInstance> h);
		bool DestroyTextureGPU(Handle<ITexture> h);

	private:
		bool CreateBasicPSO();

	private:
		RendererCreateInfo m_CreateInfo = {};
		AssetManager* m_pAssetManager = nullptr;

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		RefCntAutoPtr<IBuffer> m_pFrameCB;
		RefCntAutoPtr<IBuffer> m_pObjectCB;

		RefCntAutoPtr<IPipelineState> m_pBasicPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pBasicSRB;

		RefCntAutoPtr<ISampler> m_pDefaultSampler;

		std::unique_ptr<RenderResourceCache> m_pRenderResourceCache;
	};
} // namespace shz
