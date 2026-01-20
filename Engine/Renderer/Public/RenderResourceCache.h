#pragma once
#include <vector>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/AssetRuntime/Common/AssetID.hpp"
#include "Engine/AssetRuntime/Common/AssetRef.hpp"
#include "Engine/AssetRuntime/Common/AssetPtr.hpp"
#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"

#include "Engine/AssetRuntime/AssetData/Public/TextureAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/MaterialAsset.h"

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

		bool Initialize(IRenderDevice* pDevice, AssetManager* pAssetManager);
		void Shutdown();
		void Clear();

		Handle<TextureRenderData> GetOrCreateTextureRenderData(const TextureAsset& asset);
		Handle<TextureRenderData> GetOrCreateTextureRenderData(const AssetRef<TextureAsset>& texRef, EAssetLoadFlags flags = EAssetLoadFlags::AllowFallback);

		const TextureRenderData* TryGetTextureRenderData(Handle<TextureRenderData> h) const noexcept;
		TextureRenderData* TryGetTextureRenderData(Handle<TextureRenderData> h) noexcept;
		bool DestroyTextureRenderData(Handle<TextureRenderData> h);
		void InvalidateTextureByAsset(const TextureAsset& asset);
		void InvalidateTextureByRef(const AssetRef<TextureAsset>& texRef);

		Handle<StaticMeshRenderData> GetOrCreateStaticMeshRenderData(const StaticMeshAsset& asset, IDeviceContext* pCtx);

		const StaticMeshRenderData* TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) const noexcept;
		StaticMeshRenderData* TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) noexcept;

		bool DestroyStaticMeshRenderData(Handle<StaticMeshRenderData> h);
		void InvalidateStaticMeshByAsset(const StaticMeshAsset& asset);

		Handle<MaterialRenderData> GetOrCreateMaterialRenderData(MaterialInstance* pInstance, IDeviceContext* pCtx, IMaterialStaticBinder* pStaticBinder);

		const MaterialRenderData* TryGetMaterialRenderData(Handle<MaterialRenderData> h) const noexcept;
		MaterialRenderData* TryGetMaterialRenderData(Handle<MaterialRenderData> h) noexcept;

		bool DestroyMaterialRenderData(Handle<MaterialRenderData> h);
		void InvalidateMaterialByInstance(const MaterialInstance* pInstance);

		void SetErrorTexture(const std::string& path);
		const TextureRenderData& GetErrorTexture() const noexcept;

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
			const size_t h = std::hash<AssetID>{}(id);
			return static_cast<uint64>(h);
		}

	private:
		bool createTextureFromAsset(const TextureAsset& asset, TextureRenderData* outRD);
		bool createStaticMeshFromAsset(const StaticMeshAsset& asset, StaticMeshRenderData& outRD, IDeviceContext* pCtx);

	private:
		IRenderDevice* m_pDevice = nullptr;
		AssetManager* m_pAssetManager = nullptr;

		std::unordered_map<uint64, Handle<TextureRenderData>> m_TexAssetToRD = {};
		std::unordered_map<uint64, Handle<TextureRenderData>> m_TexIDToRD = {};
		std::vector<Slot<TextureRenderData>> m_TexRDSlots = {};

		std::unordered_map<uint64, Handle<StaticMeshRenderData>> m_MeshAssetToRD = {};
		std::vector<Slot<StaticMeshRenderData>> m_MeshRDSlots = {};

		std::unordered_map<uint64, Handle<MaterialRenderData>> m_MaterialInstToRD = {};
		std::vector<Slot<MaterialRenderData>> m_MaterialRDSlots = {};

		TextureRenderData m_ErrorTex = {};
	};

} // namespace shz
