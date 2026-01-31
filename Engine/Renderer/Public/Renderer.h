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
#include "Engine/RuntimeData/Public/TerrainHeightField.h"

#include "Engine/Renderer/Public/RenderResourceRegistry.h"

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

		std::string EnvTexturePath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sky/skyEnvHDR.dds";
		std::string DiffuseIrradianceTexPath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sky/skyDiffuseHDR.dds";
		std::string SpecularIrradianceTexPath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sky/skySpecularHDR.dds";
		std::string BrdfLUTTexPath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sky/skyBrdf.dds";
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

		// ---------------------------------------------------------------------
		// Resource factory wrappers (Renderer-owned shared resources)
		// ---------------------------------------------------------------------
		RefCntAutoPtr<ITexture> CreateTexture(
			const TextureDesc& desc,
			const TextureData* pInitData = nullptr);

		RefCntAutoPtr<IBuffer> CreateBuffer(
			const BufferDesc& desc,
			const BufferData* pInitData = nullptr);

		// ---------------------------------------------------------------------
		// Resource update wrappers
		// ---------------------------------------------------------------------
		void UpdateBuffer(
			IDeviceContext* pCtx,
			IBuffer* pBuffer,
			uint32 offsetBytes,
			uint32 sizeBytes,
			const void* pData,
			RESOURCE_STATE_TRANSITION_MODE transitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION) const;

		// Simplified texture update helper (2D)
		// NOTE: For full generality, add box/mip/slice parameters as needed.
		void UpdateTexture2D(
			IDeviceContext* pCtx,
			ITexture* pTexture,
			uint32 mipLevel,
			uint32 arraySlice,
			const TextureSubResData& subRes,
			RESOURCE_STATE_TRANSITION_MODE transitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION) const;

		// ---------------------------------------------------------------------
		// RenderData caches (unchanged)
		// ---------------------------------------------------------------------
		const TextureRenderData& CreateTextureRenderData(const AssetRef<Texture>& assetRef, const std::string& name = "");
		const TextureRenderData& CreateTextureRenderData(const Texture& texture, uint64 key = 0, const std::string& name = "");
		const MaterialRenderData& CreateMaterialRenderData(const AssetRef<Material>& assetRef, const std::string& name = "");
		const MaterialRenderData& CreateMaterialRenderData(const Material& material, uint64 key = 0, const std::string& name = "");
		const StaticMeshRenderData& CreateStaticMeshRenderData(const AssetRef<StaticMesh>& assetRef, const std::string& name = "");
		const StaticMeshRenderData& CreateStaticMeshRenderData(const StaticMesh& mesh, uint64 key = 0, const std::string& name = "");

		const TextureRenderData& CreateTextureRenderDataFromHeightField(const TerrainHeightField& terrain);

		// Registry access (Renderer getters for SRV/RTV are removed)
		RenderResourceRegistry& GetRegistry() noexcept { return m_Registry; }
		const RenderResourceRegistry& GetRegistry() const noexcept { return m_Registry; }

		const std::unordered_map<std::string, uint64> GetPassDrawCallCountTable() const;

		const MaterialTemplate& GetMaterialTemplate(const std::string& name) const;
		std::vector<std::string> GetAllMaterialTemplateNames() const;

	private:
		void uploadObjectIndexInstance(IDeviceContext* pCtx, uint32 objectIndex);
		void wirePassOutputs();
		void addPass(std::unique_ptr<RenderPassBase> pass);

		// Common shared resource ids
		static uint64 MakeResID(const char* name);

	private:
		static constexpr uint64 DEFAULT_MAX_OBJECT_COUNT = 1ull << 20;

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
		std::unique_ptr<PipelineStateManager> m_pPipelineStateManager;

		RenderResourceCache<TextureRenderData> m_TextureCache;
		RenderResourceCache<StaticMeshRenderData> m_StaticMeshCache;
		RenderResourceCache<MaterialRenderData> m_MaterialCache;

		std::unique_ptr<RendererMaterialStaticBinder> m_pGBufferMaterialStaticBinder;
		std::unique_ptr<RendererMaterialStaticBinder> m_pGrassMaterialStaticBinder;
		std::unique_ptr<RendererMaterialStaticBinder> m_pShadowMaterialStaticBinder;

		// NEW: Shared render resources live here
		RenderResourceRegistry m_Registry;

		RenderPassContext m_PassCtx = {};
		std::unordered_map<std::string, std::unique_ptr<RenderPassBase>> m_Passes;
		std::unordered_map<std::string, IRenderPass*> m_RHIRenderPasses;
		std::vector<std::string> m_PassOrder;
	};
} // namespace shz
