#pragma once
#include <vector>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"
#include "Engine/AssetRuntime/Public/MaterialInstanceAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"

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

		// ------------------------------------------------------------
		// TextureRenderData
		// ------------------------------------------------------------
		Handle<TextureRenderData> GetOrCreateTextureRenderData(Handle<TextureAsset> hTexAsset);
		const TextureRenderData* TryGetTextureRenderData(Handle<TextureRenderData> h) const noexcept;
		TextureRenderData* TryGetTextureRenderData(Handle<TextureRenderData> h) noexcept;
		bool DestroyTextureRenderData(Handle<TextureRenderData> h);
		void InvalidateTextureByAsset(Handle<TextureAsset> hTexAsset);

		// ------------------------------------------------------------
		// StaticMeshRenderData  (요청했던 4개 + invalidate)
		// ------------------------------------------------------------
		Handle<StaticMeshRenderData> GetOrCreateStaticMeshRenderData(Handle<StaticMeshAsset> hMeshAsset, IDeviceContext* pCtx);

		const StaticMeshRenderData* TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) const noexcept;
		StaticMeshRenderData* TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) noexcept;

		bool DestroyStaticMeshRenderData(Handle<StaticMeshRenderData> h);
		void InvalidateStaticMeshByAsset(Handle<StaticMeshAsset> hMeshAsset);

		// ------------------------------------------------------------
		// MaterialRenderData
		// - keyed by (MaterialInstance pointer) for now (simple & safe)
		// - if you want asset-based caching, we can extend later.
		// ------------------------------------------------------------
		Handle<MaterialRenderData> GetOrCreateMaterialRenderData(
			const MaterialInstance* pInstance,
			IPipelineState* pPSO,
			const MaterialTemplate* pTemplate);

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
		// NOTE: 실제 로더는 프로젝트마다 다르므로 여기만 너 코드로 채우면 됨.
		// 이 함수는 "구조상 반드시 필요"한 부분이라 stub로 분리.
		bool createTextureFromAsset(const TextureAsset& asset, TextureRenderData* outRD);

		bool createStaticMeshFromAsset(const StaticMeshAsset& asset, StaticMeshRenderData& outRD, IDeviceContext* pCtx);

	private:
		IRenderDevice* m_pDevice = nullptr;
		AssetManager* m_pAssetManager = nullptr;

		// Texture: TextureAssetHandleValue -> TextureRenderDataHandle
		std::unordered_map<uint64, Handle<TextureRenderData>> m_TexAssetToRD = {};
		std::vector<Slot<TextureRenderData>> m_TexRDSlots = {};

		// Mesh: StaticMeshAssetHandleValue -> StaticMeshRenderDataHandle
		std::unordered_map<uint64, Handle<StaticMeshRenderData>> m_MeshAssetToRD = {};
		std::vector<Slot<StaticMeshRenderData>> m_MeshRDSlots = {};

		// Material: instance pointer -> MaterialRenderDataHandle
		std::unordered_map<uint64, Handle<MaterialRenderData>> m_MaterialInstToRD = {};
		std::vector<Slot<MaterialRenderData>> m_MaterialRDSlots = {};
	};

} // namespace shz
