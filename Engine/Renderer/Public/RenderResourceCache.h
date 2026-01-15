#pragma once
#include <vector>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/ISampler.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Renderer/Public/MaterialInstance.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"

namespace shz
{
	// TODO: 리팩토링 필요.

	class AssetManager;

	struct RenderResourceCacheCreateInfo
	{
		RefCntAutoPtr<IRenderDevice> pDevice;
		AssetManager* pAssetManager = nullptr; // not owned
	};

	class RenderResourceCache final
	{
	public:
		RenderResourceCache() = default;
		RenderResourceCache(const RenderResourceCache&) = delete;
		RenderResourceCache& operator=(const RenderResourceCache&) = delete;
		~RenderResourceCache() { Cleanup(); };

		bool Initialize(const RenderResourceCacheCreateInfo& createInfo);
		void Cleanup();

		// ------------------------------------------------------------
		// Public APIs used by Renderer
		// ------------------------------------------------------------
		Handle<StaticMeshRenderData> CreateStaticMesh(Handle<StaticMeshAsset> h);

		bool DestroyStaticMesh(Handle<StaticMeshRenderData> h);
		bool DestroyMaterialInstance(Handle<MaterialInstance> h);
		bool DestroyTextureGPU(Handle<ITexture> h);

		const StaticMeshRenderData* TryGetMesh(Handle<StaticMeshRenderData> h) const noexcept;

		// Material SRB cache builder (needs Renderer-owned PSO + frame/object CBs)
		MaterialRenderData* GetOrCreateMaterialRenderData(
			Handle<MaterialInstance> h,
			const RefCntAutoPtr<IPipelineState>& pPSO,
			const RefCntAutoPtr<IBuffer>& pObjectCB,
			IDeviceContext* pCtx);

	private:
		// ------------------------------------------------------------
		// Internal storage policy: Slot + UniqueHandle
		// NOTE: For textures, handle type (ITexture) differs from stored value type (RefCntAutoPtr<ITexture>).
		// ------------------------------------------------------------
		template<typename T>
		struct Slot final
		{
			UniqueHandle<T> Owner = {};
			std::optional<T> Value = {};
		};

		template<typename HandleT, typename ValueT>
		struct SlotHV final
		{
			UniqueHandle<HandleT> Owner = {};
			std::optional<ValueT> Value = {};
		};

		template<typename TSlot>
		static void ensureSlotCapacity(uint32 index, std::vector<TSlot>& slots)
		{
			if (index >= static_cast<uint32>(slots.size()))
			{
				slots.resize(static_cast<size_t>(index) + 1);
			}
		}

		static Slot<StaticMeshRenderData>* findSlot(Handle<StaticMeshRenderData> h, std::vector<Slot<StaticMeshRenderData>>& slots) noexcept;
		static const Slot<StaticMeshRenderData>* findSlot(Handle<StaticMeshRenderData> h, const std::vector<Slot<StaticMeshRenderData>>& slots) noexcept;

		static Slot<MaterialInstance>* findSlot(Handle<MaterialInstance> h, std::vector<Slot<MaterialInstance>>& slots) noexcept;
		static const Slot<MaterialInstance>* findSlot(Handle<MaterialInstance> h, const std::vector<Slot<MaterialInstance>>& slots) noexcept;

		static SlotHV<ITexture, RefCntAutoPtr<ITexture>>* findTexSlot(Handle<ITexture> h, std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept;
		static const SlotHV<ITexture, RefCntAutoPtr<ITexture>>* findTexSlot(Handle<ITexture> h, const std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept;

	private:
		Handle<ITexture> createTextureGPU(Handle<TextureAsset> h);
		Handle<MaterialInstance> createMaterialInstance(Handle<MaterialAsset> h);

	private:
		RenderResourceCacheCreateInfo m_CreateInfo = {};
		AssetManager* m_pAssetManager = nullptr;

		Handle<MaterialInstance> m_DefaultMaterial = {};

		std::vector<Slot<StaticMeshRenderData>> m_MeshSlots;

		// TextureAsset -> GPU texture handle cache
		std::unordered_map<Handle<TextureAsset>, Handle<ITexture>> m_TexAssetToGpuHandle;

		// Texture slots: Handle<ITexture> -> RefCntAutoPtr<ITexture>
		std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>> m_TextureSlots;

		std::vector<Slot<MaterialInstance>> m_MaterialSlots;

		// Per material instance render binding cache (SRB etc)
		std::unordered_map<Handle<MaterialInstance>, MaterialRenderData> m_MatRenderDataTable;


		struct DefaultMaterialTextures
		{
			RefCntAutoPtr<ITexture> White;
			RefCntAutoPtr<ITexture> Normal;
			RefCntAutoPtr<ITexture> MetallicRoughness;
			RefCntAutoPtr<ITexture> AO;
			RefCntAutoPtr<ITexture> Emissive;
		};

		DefaultMaterialTextures m_DefaultTextures;
	};
} // namespace shz
