#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IEngineFactory.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/ISwapChain.h"
#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IFramebuffer.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"

#include "Engine/ImGui/Public/ImGuiImplShizen.hpp"

#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Material/Public/MaterialInstance.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"
#include "Engine/Renderer/Public/RendererMaterialStaticBinder.h"

#include "Engine/RenderPass/Public/RenderPassContext.h"
#include "Engine/RenderPass/Public/RenderPassBase.h"

namespace shz
{
	struct RendererCreateInfo
	{
		RefCntAutoPtr<IEngineFactory> pEngineFactory;
		RefCntAutoPtr<IRenderDevice>  pDevice;
		RefCntAutoPtr<IDeviceContext> pImmediateContext;
		std::vector<RefCntAutoPtr<IDeviceContext>> pDeferredContexts;
		RefCntAutoPtr<ISwapChain> pSwapChain;
		RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;

		ImGuiImplShizen* pImGui = nullptr;
		AssetManager* pAssetManager = nullptr; // not owned

		uint32 BackBufferWidth = 0;
		uint32 BackBufferHeight = 0;
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

		void BeginFrame();
		void Render(RenderScene& scene, const ViewFamily& viewFamily);
		void EndFrame();

		void ReleaseSwapChainBuffers();
		void OnResize(uint32 width, uint32 height);

		Handle<StaticMeshRenderData> CreateStaticMesh(const StaticMeshAsset& asset);
		bool DestroyStaticMesh(Handle<StaticMeshRenderData> hMesh);

		ITextureView* GetLightingSRV() const noexcept { return m_PassCtx.pLightingSrv; }
		ITextureView* GetGBufferSRV(uint32 index) const noexcept { return m_PassCtx.pGBufferSrv[index]; }
		ITextureView* GetDepthSRV() const noexcept { return m_PassCtx.pDepthSrv; }
		ITextureView* GetShadowMapSRV() const noexcept { return m_PassCtx.pShadowMapSrv; }

	private:
		bool ensureObjectTableCapacity(uint32 objectCount);
		void uploadObjectIndexInstance(IDeviceContext* pCtx, uint32 objectIndex);
		void wirePassOutputs();
		void addPass(std::unique_ptr<RenderPassBase> pass);

	private:
		static constexpr uint64 DEFAULT_MAX_OBJECT_COUNT = 1 << 16;

		RendererCreateInfo m_CreateInfo = {};
		AssetManager* m_pAssetManager = nullptr;

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		std::unique_ptr<RenderResourceCache> m_pCache;
		std::unique_ptr<RendererMaterialStaticBinder> m_pMaterialStaticBinder;

		RefCntAutoPtr<IBuffer> m_pFrameCB;
		RefCntAutoPtr<IBuffer> m_pShadowCB;

		RefCntAutoPtr<IBuffer> m_pObjectTableSB;
		RefCntAutoPtr<IBuffer> m_pObjectIndexVB;
		uint32 m_ObjectTableCapacity = 0;

		RefCntAutoPtr<ITexture> m_EnvTex;
		RefCntAutoPtr<ITexture> m_EnvDiffuseTex;
		RefCntAutoPtr<ITexture> m_EnvSpecularTex;
		RefCntAutoPtr<ITexture> m_EnvBrdfTex;

		RenderPassContext m_PassCtx = {};
		std::unordered_map<std::string, std::unique_ptr<RenderPassBase>> m_Passes;
		std::vector<std::string> m_PassOrder;
	};
} // namespace shz
