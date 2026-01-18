#pragma once
#include <vector>
#include <memory>

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

#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Material/Public/MaterialInstance.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"

namespace shz
{
	class AssetManagerBase;

	struct RendererCreateInfo
	{
		RefCntAutoPtr<IEngineFactory> pEngineFactory;
		RefCntAutoPtr<IRenderDevice>  pDevice;
		RefCntAutoPtr<IDeviceContext> pImmediateContext;
		std::vector<RefCntAutoPtr<IDeviceContext>> pDeferredContexts;
		RefCntAutoPtr<ISwapChain> pSwapChain;
		RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;

		ImGuiImplShizen* pImGui = nullptr;
		AssetManagerBase* pAssetManager = nullptr; // not owned

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
		void Render(const RenderScene& scene, const ViewFamily& viewFamily);
		void EndFrame();

		// Must be called before swap-chain resize/fullscreen toggle.
		// Releases all refs that can indirectly hold swap-chain backbuffers (framebuffers, cached RTVs, etc).
		void ReleaseSwapChainBuffers();

		// Call after swap-chain resize is completed and new width/height is known.
		void OnResize(uint32 width, uint32 height);

		// Forwarding (Asset -> RD)
		Handle<StaticMeshRenderData> CreateStaticMesh(const StaticMeshAsset& asset);
		bool DestroyStaticMesh(Handle<StaticMeshRenderData> hMesh);

	private:
		bool createShadowTargets();
		bool createDeferredTargets();

		bool createShadowRenderPasses();
		bool createDeferredRenderPasses();

		bool createShadowPso();
		bool createGBufferPso();
		bool createLightingPso();
		bool createPostPso();

		bool recreateShadowResources();
		bool recreateSizeDependentResources();

		// Framebuffer for current backbuffer (created in BeginFrame, released in EndFrame)
		bool buildPostFramebufferForCurrentBackBuffer();

		void setViewportFromView(const View& view);

	private:
		RendererCreateInfo m_CreateInfo = {};
		AssetManagerBase* m_pAssetManager = nullptr;

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		uint32 m_DeferredWidth = 0;
		uint32 m_DeferredHeight = 0;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		std::unique_ptr<RenderResourceCache> m_pCache;

		// Frame/Object/Shadow CBs
		RefCntAutoPtr<IBuffer> m_pFrameCB;
		RefCntAutoPtr<IBuffer> m_pObjectCB;
		RefCntAutoPtr<IBuffer> m_pShadowCB;

		// Targets
		static constexpr uint32 SHADOW_MAP_SIZE = 1024;

		RefCntAutoPtr<ITexture>     m_ShadowMapTex;
		RefCntAutoPtr<ITextureView> m_ShadowMapDsv;
		RefCntAutoPtr<ITextureView> m_ShadowMapSrv;

		RefCntAutoPtr<ITexture>     m_GBufferDepthTex;
		RefCntAutoPtr<ITextureView> m_GBufferDepthDSV;
		RefCntAutoPtr<ITextureView> m_GBufferDepthSRV;

		static constexpr uint32 NUM_GBUFFERS = 4;
		RefCntAutoPtr<ITexture>     m_GBufferTex[NUM_GBUFFERS];
		RefCntAutoPtr<ITextureView> m_GBufferRtv[NUM_GBUFFERS];
		RefCntAutoPtr<ITextureView> m_GBufferSrv[NUM_GBUFFERS];

		RefCntAutoPtr<ITexture>     m_LightingTex;
		RefCntAutoPtr<ITextureView> m_LightingRTV;
		RefCntAutoPtr<ITextureView> m_LightingSRV;

		// RenderPass/FB (offscreen FBs can be persistent)
		RefCntAutoPtr<IRenderPass>  m_RenderPassShadow;
		RefCntAutoPtr<IFramebuffer> m_FrameBufferShadow;

		RefCntAutoPtr<IRenderPass>  m_RenderPassGBuffer;
		RefCntAutoPtr<IFramebuffer> m_FrameBufferGBuffer;

		RefCntAutoPtr<IRenderPass>  m_RenderPassLighting;
		RefCntAutoPtr<IFramebuffer> m_FrameBufferLighting;

		RefCntAutoPtr<IRenderPass>  m_RenderPassPost;

		// IMPORTANT:
		// This framebuffer references swapchain backbuffer RTV -> must NOT be kept alive across frames.
		RefCntAutoPtr<IFramebuffer> m_FrameBufferPostCurrent;

		// PSO/SRB
		RefCntAutoPtr<IPipelineState>         m_ShadowPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_ShadowSRB;

		RefCntAutoPtr<IPipelineState> m_GBufferPSO;

		RefCntAutoPtr<IPipelineState>         m_LightingPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_LightingSRB;

		RefCntAutoPtr<IPipelineState>         m_PostPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_PostSRB;

		// Per-frame data
		std::vector<StateTransitionDesc> m_PreBarriers;
		std::vector<uint64> m_FrameMatKeys;
		std::unordered_map<uint64, Handle<MaterialRenderData>> m_FrameMat; // reused map (reserve once)

		bool m_ShadowDirty = true;
		bool m_DeferredDirty = true;
	};

} // namespace shz
