#pragma once
#include <vector>
#include <memory>
#include <unordered_set>
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
		// Resource registry wrappers
		// ---------------------------------------------------------------------
		uint64 AddTexture(const std::string& name, const TextureDesc& desc, const TextureData* pInitData = nullptr);
		uint64 AddTexture(uint64 id, const TextureDesc& desc, const TextureData* pInitData = nullptr);
		uint64 AddTexture(const std::string& name, RefCntAutoPtr<ITexture>&& tex);
		uint64 AddTexture(uint64 id, RefCntAutoPtr<ITexture>&& tex);

		uint64 AddBuffer(const std::string& name, const BufferDesc& desc, const BufferData* pInitData = nullptr);
		uint64 AddBuffer(uint64 id, const BufferDesc& desc, const BufferData* pInitData = nullptr);
		uint64 AddBuffer(const std::string& name, RefCntAutoPtr<IBuffer>&& buf);
		uint64 AddBuffer(uint64 id, RefCntAutoPtr<IBuffer>&& buf);

		uint64 AddUniformBuffer(const std::string& name, uint64 sizeBytes);
		uint64 AddUniformBuffer(uint64 id, uint64 sizeBytes);

		void AddTextureView(const std::string& textureName, const TextureViewDesc& viewDesc);
		void AddTextureView(uint64 textureId, const TextureViewDesc& viewDesc);

		// ---------------------------------------------------------------------
		// Render pass management
		// ---------------------------------------------------------------------
		void AddPass(std::unique_ptr<RenderPassBase> pass);

		// ---------------------------------------------------------------------
		// Resource factory wrappers (Renderer-owned shared resources)
		// ---------------------------------------------------------------------
		RefCntAutoPtr<ITexture> CreateTexture(const TextureDesc& desc, const TextureData* pInitData = nullptr);
		RefCntAutoPtr<IBuffer> CreateBuffer(const BufferDesc& desc, const BufferData* pInitData = nullptr);

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

		void UpdateTexture2D(
			IDeviceContext* pCtx,
			ITexture* pTexture,
			uint32 arraySlice,
			const Texture& sourceImage,
			RESOURCE_STATE_TRANSITION_MODE transitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION) const;

		// ---------------------------------------------------------------------
		// RenderData caches (unchanged)
		// ---------------------------------------------------------------------
		RefCntAutoPtr<ITexture> CreateTextureRenderData(const AssetRef<Texture>& assetRef);
		RefCntAutoPtr<ITexture> CreateTextureRenderData(const std::string& name, const Texture& texture);
		RefCntAutoPtr<ITexture> CreateTextureRenderData(uint64 id, const Texture& texture);
		const StaticMeshRenderData& CreateStaticMeshRenderData(const AssetRef<StaticMesh>& assetRef, const std::string& name = "");
		const StaticMeshRenderData& CreateStaticMeshRenderData(const StaticMesh& mesh, uint64 key = 0, const std::string& name = "");
		RefCntAutoPtr<ITexture> CreateTextureRenderDataFromHeightField(const TerrainHeightField& terrain);

		const MaterialTemplate& GetMaterialTemplate(const std::string& name) const;
		std::vector<std::string> GetAllMaterialTemplateNames() const;

	private:
		RefCntAutoPtr<IPipelineState> acquirePipelineStateFromMaterial(MaterialId id, uint64 renderPassKey = 0) const;
		RefCntAutoPtr<IShaderResourceBinding> acquireShaderResourceBindingFromMaterial(MaterialId id, IPipelineState* pso);

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

		RenderResourceCache<StaticMeshRenderData> m_StaticMeshCache;
		
		struct PipelineBinding
		{
			RefCntAutoPtr<IPipelineState> pPSO;
			RefCntAutoPtr<IShaderResourceBinding> pSRB;
		};
		std::unordered_map<uint64, PipelineBinding> m_PipelineBindingCache;

		std::unordered_set<RefCntAutoPtr<IBuffer>> m_NewBuffersThisFrame;
		std::unordered_set<RefCntAutoPtr<ITexture>> m_NewTexturesThisFrame;

		std::unique_ptr<RenderResourceRegistry> m_pRegistry;

		RenderPassContext m_PassCtx = {};
		std::unordered_map<std::string, std::unique_ptr<RenderPassBase>> m_Passes;
		std::unordered_map<uint64, IRenderPass*> m_RHIRenderPasses;
		std::vector<std::string> m_PassOrder;
	};
} // namespace shz
