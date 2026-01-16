#pragma once
#include <vector>
#include <memory>

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
		RefCntAutoPtr<IRenderDevice>  pDevice;
		RefCntAutoPtr<IDeviceContext> pImmediateContext;
		std::vector<RefCntAutoPtr<IDeviceContext>> pDeferredContexts;
		RefCntAutoPtr<ISwapChain> pSwapChain;

		ImGuiImplShizen* pImGui = nullptr;

		// AssetManager reference (not owned).
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

		// Resource-facing API (implemented by RenderResourceCache).
		Handle<StaticMeshRenderData> CreateStaticMesh(Handle<StaticMeshAsset> h);

		bool DestroyStaticMesh(Handle<StaticMeshRenderData> h);
		bool DestroyMaterialInstance(Handle<MaterialInstance> h);
		bool DestroyTextureGPU(Handle<ITexture> h);

	private:
		bool createBasicPso();
		bool createGBufferPso();
		bool createLightingPso();
		bool createPostPso();
		bool createShadowPso();

		bool createShadowTargets();
		bool createDeferredTargets();

		bool createShadowRenderPasses();
		bool createDeferredRenderPasses();

	private:
		RendererCreateInfo m_CreateInfo = {};
		AssetManager* m_pAssetManager = nullptr;

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		RefCntAutoPtr<IBuffer> m_pFrameCb;
		RefCntAutoPtr<IBuffer> m_pObjectCb;
		RefCntAutoPtr<IBuffer> m_pShadowCb;

		RefCntAutoPtr<IPipelineState> m_PsoBasic;

		std::unique_ptr<RenderResourceCache> m_pRenderResourceCache;

		// ------------------------------------------------------------
		// Deferred resources (Shadow -> GBuffer -> Lighting -> Post)
		// ------------------------------------------------------------
		static constexpr uint32 kShadowMapSize = 4096;

		RefCntAutoPtr<ITexture>     m_ShadowMapTex;
		RefCntAutoPtr<ITextureView> m_ShadowMapDsv;
		RefCntAutoPtr<ITextureView> m_ShadowMapSrv;

		RefCntAutoPtr<ITexture>     m_GBufferDepthTex;
		RefCntAutoPtr<ITextureView> m_GBufferDepthDsv;
		RefCntAutoPtr<ITextureView> m_GBufferDepthSrv;

		static constexpr uint32 kGBufferCount = 4;
		RefCntAutoPtr<ITexture>     m_GBufferTex[kGBufferCount];
		RefCntAutoPtr<ITextureView> m_GBufferRtv[kGBufferCount];
		RefCntAutoPtr<ITextureView> m_GBufferSrv[kGBufferCount];

		RefCntAutoPtr<ITexture>     m_LightingTex;
		RefCntAutoPtr<ITextureView> m_LightingRtv;
		RefCntAutoPtr<ITextureView> m_LightingSrv;

		// Render passes + framebuffers.
		RefCntAutoPtr<IRenderPass>  m_RpShadow;
		RefCntAutoPtr<IFramebuffer> m_FbShadow;

		RefCntAutoPtr<IRenderPass>  m_RpGBuffer;
		RefCntAutoPtr<IFramebuffer> m_FbGBuffer;

		RefCntAutoPtr<IRenderPass>  m_RpLighting;
		RefCntAutoPtr<IFramebuffer> m_FbLighting;

		RefCntAutoPtr<IRenderPass>  m_RpPost;
		RefCntAutoPtr<IFramebuffer> m_FbPost; // Rebuilt every frame (backbuffer changes).

		// PSOs / SRBs.
		RefCntAutoPtr<IPipelineState>         m_PsoShadow;
		RefCntAutoPtr<IShaderResourceBinding> m_SrbShadow;

		RefCntAutoPtr<IPipelineState> m_PsoGBuffer;

		RefCntAutoPtr<IPipelineState>         m_PsoLighting;
		RefCntAutoPtr<IShaderResourceBinding> m_SrbLighting;

		RefCntAutoPtr<IPipelineState>         m_PsoPost;
		RefCntAutoPtr<IShaderResourceBinding> m_SrbPost;

		// Bookkeeping.
		uint32 m_DeferredWidth = 0;
		uint32 m_DeferredHeight = 0;
	};
} // namespace shz
