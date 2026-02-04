#pragma once
#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/RuntimeData/Public/StaticMesh.h"

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

#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"
#include "Engine/Renderer/Public/RenderResourceCache.hpp"
#include "Engine/Renderer/Public/PipelineStateManager.h"

#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
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

		// Render pass management
		struct RenderPassItem
		{
			std::string Name;
			std::vector<RenderPassResourceAccess> ResourceAccess;
			std::function<void(RenderPassContext&)> ExecuteLambda;

			std::vector<OptimizedClearValue> ClearValues;

			RefCntAutoPtr<IRenderPass> pRHIRenderpass;
			RefCntAutoPtr<IFramebuffer> pRHIFramebuffer;
			std::vector<ITextureView*> StaticFBAttachments;
			bool bUseSwapChainBackBuffer = false;
		};

		void AddPass(
			const std::string& name, 
			std::function<void(RenderPassBuilder&)> buildLambda, 
			std::function<void(RenderPassContext&)> executeLambda,
			std::function<void()> onCreated = {});

		// Resource registry wrappers
		RefCntAutoPtr<ITexture> GetTexture(uint64 id) const;
		RefCntAutoPtr<ITextureView> GetTextureSRV(uint64 id) const;
		RefCntAutoPtr<ITextureView> GetTextureRTV(uint64 id) const;
		RefCntAutoPtr<ITextureView> GetTextureDSV(uint64 id) const;
		RefCntAutoPtr<ITextureView> GetTextureUAV(uint64 id) const;

		RefCntAutoPtr<IBuffer> GetBuffer(uint64 id) const;
		RefCntAutoPtr<IBufferView> GetBufferSRV(uint64 id) const;
		RefCntAutoPtr<IBufferView> GetBufferUAV(uint64 id) const;

		uint64 AddTexture(const std::string& name, const TextureDesc& desc, const TextureData* pInitData = nullptr);
		uint64 AddTexture(uint64 id, const TextureDesc& desc, const TextureData* pInitData = nullptr);
		uint64 AddTexture(const std::string& name, RefCntAutoPtr<ITexture>&& tex);
		uint64 AddTexture(uint64 id, RefCntAutoPtr<ITexture>&& tex);

		void AddTextureView(const std::string& textureName, const TextureViewDesc& viewDesc);
		void AddTextureView(uint64 textureId, const TextureViewDesc& viewDesc);

		uint64 AddBuffer(const std::string& name, const BufferDesc& desc, const BufferData* pInitData = nullptr);
		uint64 AddBuffer(uint64 id, const BufferDesc& desc, const BufferData* pInitData = nullptr);
		uint64 AddBuffer(const std::string& name, RefCntAutoPtr<IBuffer>&& buf);
		uint64 AddBuffer(uint64 id, RefCntAutoPtr<IBuffer>&& buf);

		uint64 AddUniformBuffer(const std::string& name, uint64 sizeBytes);
		uint64 AddUniformBuffer(uint64 id, uint64 sizeBytes);

		template<typename T>
		void UpdateBuffer(uint64 id, const T& data)
		{
			ASSERT(m_pRegistry->HasBuffer(id), "Buffer not found.");
			ASSERT(m_pRegistry->GetBuffer(id)->GetDesc().Size >= sizeof(T), "Data size mistmatch.");
			BufferUpdateDesc bud = {};
			bud.ResourceId = id;
			bud.Data.resize(sizeof(T));
			std::memcpy(bud.Data.data(), &data, sizeof(T));
			m_PendingBufferUpdates.emplace_back(bud);
		}

		// RenderData 
		const StaticMeshRenderData& CreateStaticMeshRenderData(const AssetRef<StaticMesh>& assetRef, const std::string& name = "");
		const StaticMeshRenderData& CreateStaticMeshRenderData(const StaticMesh& mesh, uint64 key = 0, const std::string& name = "");

		// Shader
		void CreateShader(ShaderCreateInfo& sci, IShader** ppOutShader);

		// PipelineState
		RefCntAutoPtr<IPipelineState> AcquirePipelineState(const GraphicsPipelineStateCreateInfo& desc, bool bBindCommonResources = true);
		RefCntAutoPtr<IPipelineState> AcquirePipelineState(const ComputePipelineStateCreateInfo& desc, bool bBindCommonResources = true);
		RefCntAutoPtr<IPipelineState> AcquirePipelineState(uint64 passId, GraphicsPipelineStateCreateInfo& desc, bool bBindCommonResources = true);
		RefCntAutoPtr<IPipelineState> AcquirePipelineState(uint64 passId, ComputePipelineStateCreateInfo& desc, bool bBindCommonResources = true);

		// Material templates
		const MaterialTemplate& GetMaterialTemplate(const std::string& name) const;
		std::vector<std::string> GetAllMaterialTemplateNames() const;

		// Shadow pso, srb
		void SetShadowPipeline(RefCntAutoPtr<IPipelineState> pOpaquePSO, RefCntAutoPtr<IPipelineState> pMaskedPSO);

	private:
		void pushBarrier(IDeviceObject* pObj, RESOURCE_STATE from, RESOURCE_STATE to);

		RefCntAutoPtr<ITexture> createTexture(const TextureDesc& desc, const TextureData* pInitData = nullptr);
		RefCntAutoPtr<ITexture> createTexture(const AssetRef<Texture>& assetRef);
		RefCntAutoPtr<ITexture> createTexture(const std::string& name, const Texture& texture);
		RefCntAutoPtr<ITexture> createTexture(uint64 id, const Texture& texture);

		RefCntAutoPtr<IBuffer> createBuffer(const BufferDesc& desc, const BufferData* pInitData = nullptr);

		void updateTexture2D(IDeviceContext* pCtx, ITexture* pTexture, uint32 arraySlice, const Texture& sourceImage, RESOURCE_STATE_TRANSITION_MODE transitionMode) const;

		RefCntAutoPtr<IPipelineState> acquirePipelineStateFromMaterial(MaterialId id, uint64 renderPassKey = 0) const;
		RefCntAutoPtr<IShaderResourceBinding> acquireShaderResourceBindingFromMaterial(MaterialId id, IPipelineState* pso);

		// Render graph
		void compileRenderGraphOrder();
		void buildTransitionsForPass(uint64 passId, std::vector<StateTransitionDesc>& outBarriers);
		RESOURCE_STATE mapUsageToState(const RenderPassResourceAccess& a) const;
		IDeviceObject* resolveDeviceObject(const RenderPassResourceAccess& a) const;

	private:
		RefCntAutoPtr<IFramebuffer> m_pSwapChainFramebuffer; 
		RefCntAutoPtr<IRenderPass> m_pPresentRenderPass;

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

		struct PendingBarrier final
		{
			RefCntAutoPtr<IDeviceObject> Hold;
			StateTransitionDesc Desc;
		};
		std::vector<PendingBarrier> m_PendingBarriers;

		struct BufferUpdateDesc
		{
			uint64 ResourceId;
			std::vector<uint8> Data;
		};
		std::vector<BufferUpdateDesc> m_PendingBufferUpdates;

		std::unique_ptr<RenderResourceRegistry> m_pRegistry;

		RenderPassContext m_PassCtx = {};
		std::unordered_map<uint64, RenderPassItem> m_PassTable;
		std::unordered_map<uint64, std::string> m_PassNameTable;
		bool m_bRenderGraphDirty = true;
		std::vector<uint64> m_CompiledPassOrder = {};
		std::unordered_map<uint64, RESOURCE_STATE> m_ResourceStates = {};
		std::unordered_map<const IDeviceObject*, RESOURCE_STATE> m_ExternalStates;

		RefCntAutoPtr<IPipelineState> m_pShadowOpaquePSO;
		RefCntAutoPtr<IPipelineState> m_pShadowMaskedPSO;
	};
} // namespace shz
