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

		bool DestroyStaticMesh(Handle<StaticMeshRenderData> h);
		bool DestroyMaterialInstance(Handle<MaterialInstance> h);
		bool DestroyTextureGPU(Handle<ITexture> h);

	private:

		void renderForward(const RenderScene& scene, const ViewFamily& viewFamily);

		bool createBasicPSO();
		bool createGBufferPSO();
		bool createLightingPSO();
		bool createPostPSO();
		bool createShadowPSO();

		bool createShadowTargets();
		bool createShadowRenderPasses();

		bool createDeferredTargets();
		bool createDeferredRenderPasses();

	private:
		RendererCreateInfo m_CreateInfo = {};
		AssetManager* m_pAssetManager = nullptr;

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		RefCntAutoPtr<IBuffer> m_pFrameCB;
		RefCntAutoPtr<IBuffer> m_pObjectCB;
		RefCntAutoPtr<IBuffer> m_pShadowCB;

		RefCntAutoPtr<IPipelineState> m_pBasicPSO;

		std::unique_ptr<RenderResourceCache> m_pRenderResourceCache;

		// ------------------------------------------------------------
		// Deferred resources (Shadow -> GBuffer -> Lighting -> Post)
		// ------------------------------------------------------------
		static constexpr uint32 kShadowMapSize = 4096;
		RefCntAutoPtr<ITexture>     m_ShadowMapTex;
		RefCntAutoPtr<ITextureView> m_ShadowMapDSV;
		RefCntAutoPtr<ITextureView> m_ShadowMapSRV;

		RefCntAutoPtr<ITexture>     m_GBufferDepthTex;
		RefCntAutoPtr<ITextureView> m_GBufferDepthDSV;
		RefCntAutoPtr<ITextureView> m_GBufferDepthSRV;

		static constexpr uint32 kGBufferCount = 4;
		RefCntAutoPtr<ITexture>     m_GBufferTex[kGBufferCount];
		RefCntAutoPtr<ITextureView> m_GBufferRTV[kGBufferCount];
		RefCntAutoPtr<ITextureView> m_GBufferSRV[kGBufferCount];

		RefCntAutoPtr<ITexture>     m_LightingTex;
		RefCntAutoPtr<ITextureView> m_LightingRTV;
		RefCntAutoPtr<ITextureView> m_LightingSRV;

		// Render passes + framebuffers
		RefCntAutoPtr<IRenderPass>  m_RP_Shadow;
		RefCntAutoPtr<IFramebuffer> m_FB_Shadow;

		RefCntAutoPtr<IRenderPass>  m_RP_GBuffer;
		RefCntAutoPtr<IFramebuffer> m_FB_GBuffer;

		RefCntAutoPtr<IRenderPass>  m_RP_Lighting;
		RefCntAutoPtr<IFramebuffer> m_FB_Lighting;

		RefCntAutoPtr<IRenderPass>  m_RP_Post;
		RefCntAutoPtr<IFramebuffer> m_FB_Post; // rebuilt every frame (backbuffer changes)

		// Shadow PSO
		RefCntAutoPtr<IPipelineState> m_PSO_Shadow;
		RefCntAutoPtr<IShaderResourceBinding> m_SRB_Shadow;

		// G-Buffer PSO
		RefCntAutoPtr<IPipelineState> m_PSO_GBuffer;

		// Fullscreen PSOs
		RefCntAutoPtr<IPipelineState> m_PSO_Lighting;
		RefCntAutoPtr<IShaderResourceBinding> m_SRB_Lighting;

		RefCntAutoPtr<IPipelineState> m_PSO_Post;
		RefCntAutoPtr<IShaderResourceBinding> m_SRB_Post;

		// Bookkeeping
		uint32 m_DeferredWidth = 0;
		uint32 m_DeferredHeight = 0;
	};
} // namespace shz
