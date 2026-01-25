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

#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderResourceCache.hpp"
#include "Engine/Renderer/Public/RendererMaterialStaticBinder.h"
#include "Engine/Renderer/Public/PipelineStateManager.h"

#include "Engine/RenderPass/Public/RenderPassContext.h"
#include "Engine/RenderPass/Public/RenderPassBase.h"

#include "Engine/Renderer/Public/RenderData.h"

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

		const TextureRenderData& CreateTexture(const AssetRef<Texture>& assetRef, const std::string& name = "");
		const TextureRenderData& CreateTexture(const Texture& asset, uint64 key = 0, const std::string& name = "");
		const MaterialRenderData& CreateMaterial(const AssetRef<Material>& assetRef, const std::string& name = "");
		const MaterialRenderData& CreateMaterial(const Material& asset, uint64 key = 0, const std::string& name = "");
		const StaticMeshRenderData& CreateStaticMesh(const AssetRef<StaticMesh>& assetRef, const std::string& name = "");
		const StaticMeshRenderData& CreateStaticMesh(const StaticMesh& asset, uint64 key = 0, const std::string& name = "");

		ITextureView* GetLightingSRV() const noexcept { return m_PassCtx.pLightingSrv; }
		ITextureView* GetGBufferSRV(uint32 index) const noexcept { return m_PassCtx.pGBufferSrv[index]; }
		ITextureView* GetDepthSRV() const noexcept { return m_PassCtx.pDepthSrv; }
		ITextureView* GetShadowMapSRV() const noexcept { return m_PassCtx.pShadowMapSrv; }

		const std::unordered_map<std::string, uint64> GetPassDrawCallCountTable() const;

		const MaterialTemplate& GetMaterialTemplate(const std::string& name) const;
		std::vector<std::string> GetAllMaterialTemplateNames() const;

	private:
		void uploadObjectIndexInstance(IDeviceContext* pCtx, uint32 objectIndex);
		void wirePassOutputs();
		void addPass(std::unique_ptr<RenderPassBase> pass);

	private:
		static constexpr uint64 DEFAULT_MAX_OBJECT_COUNT = 1 << 20;

		RendererCreateInfo m_CreateInfo = {};
		RefCntAutoPtr<IRenderDevice> m_pDevice;
		RefCntAutoPtr<IDeviceContext> m_pImmediateContext;
		std::vector<RefCntAutoPtr<IDeviceContext>> m_pDeferredContexts;
		RefCntAutoPtr<ISwapChain> m_pSwapChain;

		AssetManager* m_pAssetManager = nullptr;
		std::unordered_map<std::string, MaterialTemplate> m_TemplateLibrary = {};

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		std::unique_ptr< PipelineStateManager> m_pPipelineStateManager;

		RenderResourceCache<TextureRenderData> m_TextureCache;
		RenderResourceCache<StaticMeshRenderData> m_StaticMeshCache;
		RenderResourceCache<MaterialRenderData> m_MaterialCache;

		TextureRenderData m_ErrorTexture;

		std::unique_ptr<RendererMaterialStaticBinder> m_pMaterialStaticBinder;

		RefCntAutoPtr<IBuffer> m_pFrameCB;
		RefCntAutoPtr<IBuffer> m_pDrawCB;
		RefCntAutoPtr<IBuffer> m_pShadowCB;

		RefCntAutoPtr<IBuffer> m_pObjectTableSB;
		RefCntAutoPtr<IBuffer> m_pObjectTableSBShadow;
		RefCntAutoPtr<IBuffer> m_pObjectIndexVB;

		RefCntAutoPtr<ITexture> m_EnvTex;
		RefCntAutoPtr<ITexture> m_EnvDiffuseTex;
		RefCntAutoPtr<ITexture> m_EnvSpecularTex;
		RefCntAutoPtr<ITexture> m_EnvBrdfTex;

		RenderPassContext m_PassCtx = {};
		std::unordered_map<std::string, std::unique_ptr<RenderPassBase>> m_Passes;
		std::unordered_map<std::string, IRenderPass*> m_RHIRenderPasses;
		std::vector<std::string> m_PassOrder;
	};
} // namespace shz
