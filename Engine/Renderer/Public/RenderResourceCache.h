#pragma once
#include <vector>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/AssetRuntime/Public/AssetID.hpp"
#include "Engine/AssetRuntime/Public/AssetRef.hpp"
#include "Engine/AssetRuntime/Public/AssetPtr.hpp"
#include "Engine/AssetRuntime/Public/AssetManager.h"

#include "Engine/AssetRuntime/Public/TextureAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/Public/MaterialInstanceAsset.h"

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"

#include "Engine/Renderer/Public/TextureRenderData.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"

namespace shz
{
	class RenderResourceCache final
	{
	public:
		RenderResourceCache() = default;
		RenderResourceCache(const RenderResourceCache&) = delete;
		RenderResourceCache& operator=(const RenderResourceCache&) = delete;
		~RenderResourceCache() = default;

		bool Initialize(IRenderDevice* pDevice);
		void Shutdown();
		void Clear();

		// New asset manager hook (optional, but required for AssetRef-based texture path)
		void SetAssetManager(AssetManager* pAssetManager) noexcept { m_pAssetManager = pAssetManager; }
		AssetManager* GetAssetManager() const noexcept { return m_pAssetManager; }

		// ------------------------------------------------------------
		// TextureRenderData
		// - 기존: Cache key = TextureAsset pointer (address)
		// - 신규: AssetRef 기반을 위해 AssetID 기반 key 캐시도 제공
		// ------------------------------------------------------------
		Handle<TextureRenderData> GetOrCreateTextureRenderData(const TextureAsset& asset);

		// NEW: AssetRef 기반 진입점 (MaterialInstance -> TextureBinding::TextureRef)
		Handle<TextureRenderData> GetOrCreateTextureRenderData(const AssetRef<TextureAsset>& texRef, EAssetLoadFlags flags = EAssetLoadFlags::AllowFallback);

		const TextureRenderData* TryGetTextureRenderData(Handle<TextureRenderData> h) const noexcept;
		TextureRenderData* TryGetTextureRenderData(Handle<TextureRenderData> h) noexcept;
		bool DestroyTextureRenderData(Handle<TextureRenderData> h);
		void InvalidateTextureByAsset(const TextureAsset& asset);

		// NEW: AssetID 기반 invalidate (optional)
		void InvalidateTextureByRef(const AssetRef<TextureAsset>& texRef);

		// ------------------------------------------------------------
		// StaticMeshRenderData
		// - Cache key: StaticMeshAsset pointer (address)
		// ------------------------------------------------------------
		Handle<StaticMeshRenderData> GetOrCreateStaticMeshRenderData(const StaticMeshAsset& asset, IDeviceContext* pCtx);

		const StaticMeshRenderData* TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) const noexcept;
		StaticMeshRenderData* TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) noexcept;

		bool DestroyStaticMeshRenderData(Handle<StaticMeshRenderData> h);
		void InvalidateStaticMeshByAsset(const StaticMeshAsset& asset);

		// ------------------------------------------------------------
		// MaterialRenderData
		// - Cache key: MaterialInstance pointer (address)
		// ------------------------------------------------------------
		Handle<MaterialRenderData> GetOrCreateMaterialRenderData(const MaterialInstance* pInstance, IPipelineState* pPSO, const MaterialTemplate* pTemplate);

		const MaterialRenderData* TryGetMaterialRenderData(Handle<MaterialRenderData> h) const noexcept;
		MaterialRenderData* TryGetMaterialRenderData(Handle<MaterialRenderData> h) noexcept;

		bool DestroyMaterialRenderData(Handle<MaterialRenderData> h);
		void InvalidateMaterialByInstance(const MaterialInstance* pInstance);

	private:
		template<class T>
		struct Slot final
		{
			UniqueHandle<T>  Owner = {};
			std::optional<T> Value = {};
		};

		template<class T>
		static void EnsureSlotCapacity(uint32 index, std::vector<Slot<T>>& slots)
		{
			if (index >= static_cast<uint32>(slots.size()))
			{
				slots.resize(static_cast<size_t>(index) + 1);
			}
		}

		template<class T>
		static Slot<T>* FindSlot(Handle<T> h, std::vector<Slot<T>>& slots) noexcept
		{
			if (!h.IsValid())
				return nullptr;

			const uint32 index = h.GetIndex();
			if (index == 0 || index >= static_cast<uint32>(slots.size()))
				return nullptr;

			auto& slot = slots[index];

			if (!slot.Value.has_value())
				return nullptr;

			if (slot.Owner.Get() != h)
				return nullptr;

			return &slot;
		}

		template<class T>
		static const Slot<T>* FindSlot(Handle<T> h, const std::vector<Slot<T>>& slots) noexcept
		{
			if (!h.IsValid())
				return nullptr;

			const uint32 index = h.GetIndex();
			if (index == 0 || index >= static_cast<uint32>(slots.size()))
				return nullptr;

			const auto& slot = slots[index];

			if (!slot.Value.has_value())
				return nullptr;

			if (slot.Owner.Get() != h)
				return nullptr;

			return &slot;
		}

	private:
		static inline uint64 ptrKey(const void* p) noexcept
		{
			return static_cast<uint64>(reinterpret_cast<uintptr_t>(p));
		}

		static inline uint64 assetIDKey(const AssetID& id) noexcept
		{
			// Engine already has std::hash<AssetID> specialization (per your snapshot).
			const size_t h = std::hash<AssetID>{}(id);
			return static_cast<uint64>(h);
		}

	private:
		bool createTextureFromAsset(const TextureAsset& asset, TextureRenderData* outRD);
		bool createStaticMeshFromAsset(const StaticMeshAsset& asset, StaticMeshRenderData& outRD, IDeviceContext* pCtx);

	private:
		IRenderDevice* m_pDevice = nullptr;

		// NEW: required to resolve AssetRef<TextureAsset> -> CPU TextureAsset
		AssetManager* m_pAssetManager = nullptr;

		// Texture (legacy pointer-key): TextureAsset* -> TextureRenderDataHandle
		std::unordered_map<uint64, Handle<TextureRenderData>> m_TexAssetToRD = {};
		// Texture (new AssetID-key): AssetID(hash) -> TextureRenderDataHandle
		std::unordered_map<uint64, Handle<TextureRenderData>> m_TexIDToRD = {};

		std::vector<Slot<TextureRenderData>> m_TexRDSlots = {};

		// Mesh: StaticMeshAsset* -> StaticMeshRenderDataHandle
		std::unordered_map<uint64, Handle<StaticMeshRenderData>> m_MeshAssetToRD = {};
		std::vector<Slot<StaticMeshRenderData>> m_MeshRDSlots = {};

		// Material: instance pointer -> MaterialRenderDataHandle
		std::unordered_map<uint64, Handle<MaterialRenderData>> m_MaterialInstToRD = {};
		std::vector<Slot<MaterialRenderData>> m_MaterialRDSlots = {};
	};

} // namespace shz
