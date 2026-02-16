#pragma once
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/Core/Math/Math.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Renderer/Public/DrawPacket.h"

#include "Engine/RHI/Interface/IBuffer.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	class RenderScene final
	{
	public:
		// ------------------------------------------------------------
		// SceneObject
		// ------------------------------------------------------------
		struct SceneObject final
		{
			const StaticMeshRenderData* pMesh = nullptr;

			Matrix4x4 World = {};
			Matrix4x4 WorldInvTranspose = {};

			bool bCastShadow = true;
			bool bDepthPrepass = true;
		};

		// ------------------------------------------------------------
		// TerrainObject
		// ------------------------------------------------------------
		struct TerrainObject final
		{
			RefCntAutoPtr<IBuffer> VertexBuffer = {};
			RefCntAutoPtr<IBuffer> IndexBuffer = {};

			uint32 IndexCount = 0;
			VALUE_TYPE IndexType = VT_UINT32;

			uint32 StartInstanceLocation = 0;
			uint32 InstanceCount = 0;

			MaterialId MaterialId = 0;

			bool bCastShadow = false;
		};

		struct LightObject final
		{
			uint32 Type = 0;
			float3 Color = { 1.0f, 1.0f, 1.0f };
			float  Intensity = 1.0f;

			float3 Position = { 0.0f, 0.0f, 0.0f };
			float3 Direction = { 0.0f, -1.0f, 0.0f };

			float Range = 10.0f;
			float SpotAngle = 30.0f;

			bool CastShadow = false;
		};

		struct IndirectObjectDesc final
		{
			const StaticMeshLevelRenderData* pMesh = nullptr;
			bool bCastShadow = true;
			bool bDepthPrepass = true;

			uint64 PassKey = 0;

			uint32 StartInstanceLocation = 0;

			// Base slot for this mesh's sections (slot = BaseSlot + sectionIndex)
			uint32 IndirectBaseSlot = 0;

			// Mesh id for instance counting (GPU increments this).
			uint32 IndirectMeshId = 0;
		};

		struct IndirectObject final
		{
			IndirectObjectDesc Desc = {};
			bool bEnabled = true;
		};

		// ------------------------------------------------------------
		// View params for LOD selection in BuildDrawPackets
		// ------------------------------------------------------------
		struct ViewLodParams final
		{
			Matrix4x4 View = Matrix4x4::Identity(); // World -> View
			float TanHalfFovY = 1.0f;
			float ViewportHeight = 1080.0f; // pixels (optional for future)
			bool bViewForwardIsPositiveZ = true; // if false, forward is -Z
		};

		// ------------------------------------------------------------
		// Visibility outputs
		// ------------------------------------------------------------
		struct VisibilityLists final
		{
			std::vector<uint32> VisibleDenseMain = {};
			std::vector<uint32> VisibleDenseShadow = {};

			void Clear()
			{
				VisibleDenseMain.clear();
				VisibleDenseShadow.clear();
			}
		};

	public:
		RenderScene() = default;
		RenderScene(const RenderScene&) = delete;
		RenderScene& operator=(const RenderScene&) = delete;
		~RenderScene() = default;

		void Reset();

		// ------------------------------------------------------------
		// Scene Objects
		// ------------------------------------------------------------
		Handle<SceneObject> AddObject(
			const StaticMeshRenderData& rd,
			const Matrix4x4& transform = Matrix4x4::Identity(),
			bool bCastShadow = true);
		void RemoveObject(Handle<SceneObject> h);
		void UpdateObjectMesh(Handle<SceneObject> h, const StaticMeshRenderData& mesh);
		void UpdateObjectTransform(Handle<SceneObject> h, const Matrix4x4& world);

		SceneObject* GetObjectOrNull(Handle<SceneObject> h) noexcept;
		const SceneObject* GetObjectOrNull(Handle<SceneObject> h) const noexcept;

		uint32 GetObjectCount() const noexcept { return static_cast<uint32>(m_ObjectDense.size()); }

		// ------------------------------------------------------------
		// Terrain Objects
		// ------------------------------------------------------------
		Handle<TerrainObject> AddTerrain(const TerrainObject& obj);
		void RemoveTerrain(Handle<TerrainObject> h);

		TerrainObject* GetTerrainOrNull(Handle<TerrainObject> h) noexcept;
		const TerrainObject* GetTerrainOrNull(Handle<TerrainObject> h) const noexcept;

		uint32 GetTerrainCount() const noexcept { return static_cast<uint32>(m_TerrainDense.size()); }
		const std::vector<TerrainObject>& GetTerrains() const noexcept { return m_TerrainDense; }

		// ------------------------------------------------------------
		// Indirect / Lights
		// ------------------------------------------------------------
		Handle<IndirectObject> AddIndirect(const IndirectObjectDesc& desc);
		void RemoveIndirect(Handle<IndirectObject> h);
		IndirectObject* GetIndirectOrNull(Handle<IndirectObject> h) noexcept;
		const IndirectObject* GetIndirectOrNull(Handle<IndirectObject> h) const noexcept;
		uint32 GetIndirectCount() const noexcept { return static_cast<uint32>(m_IndirectDense.size()); }
		const std::vector<IndirectObject>& GetIndirectObjects() const noexcept { return m_IndirectDense; }

		Handle<LightObject> AddLight(const LightObject& light);
		void RemoveLight(Handle<LightObject> h);
		void UpdateLight(Handle<LightObject> h, const LightObject& light);

		LightObject* GetLightOrNull(Handle<LightObject> h) noexcept;
		const LightObject* GetLightOrNull(Handle<LightObject> h) const noexcept;

		uint32 GetLightCount() const noexcept { return static_cast<uint32>(m_LightDense.size()); }
		const std::vector<LightObject>& GetLights() const noexcept { return m_LightDense; }

		// ------------------------------------------------------------
		// ObjectConstants
		// ------------------------------------------------------------
		const std::vector<hlsl::ObjectConstants>& GetObjectConstantsTableCPU() const noexcept { return m_ObjectTableCPU; }

		const std::vector<uint32>& GetDirtyOcIndices() const noexcept { return m_DirtyOcIndices; }
		void ClearDirtyOcIndices();

		uint32 GetObjectDenseCount() const noexcept { return static_cast<uint32>(m_ObjectDense.size()); }

		const SceneObject& GetObjectByDenseIndex(uint32 denseIndex) const noexcept
		{
			ASSERT(denseIndex < static_cast<uint32>(m_ObjectDense.size()), "Object dense index OOB.");
			return m_ObjectDense[denseIndex].Obj;
		}

		uint32 GetOcIndexByDenseIndex(uint32 denseIndex) const noexcept
		{
			ASSERT(denseIndex < static_cast<uint32>(m_ObjectDense.size()), "Object dense index OOB.");
			return m_ObjectDense[denseIndex].OcIndex;
		}

		// ------------------------------------------------------------
		// Visibility
		// ------------------------------------------------------------
		// Computes visible object dense indices for main/shadow frusta.
		// - Main list includes all objects whose bounds intersect frustumMain.
		// - Shadow list includes only objects that cast shadow AND intersect frustumShadow.
		void BuildVisibilityLists(
			const ViewFrustumExt& frustumMain,
			const ViewFrustumExt& frustumShadow,
			VisibilityLists& outLists) const;

		// Convenience overload (if you only need main frustum).
		void BuildVisibilityMainOnly(
			const ViewFrustumExt& frustumMain,
			std::vector<uint32>& outVisibleDenseMain) const;

		// ------------------------------------------------------------
		// Static mesh draw packets
		// ------------------------------------------------------------
		void BuildDrawPackets(
			uint64 passKey,
			const ViewLodParams& view,
			const std::vector<uint32>& visibleObjectDenseIndices,
			const std::function<const struct MaterialPipelineBinding& (MaterialId, uint64)>& resolver,
			std::vector<DrawPacket>& outPackets,
			std::vector<uint32>& outInstanceRemap) const;

		void BuildIndirectDrawPackets(
			uint64 passKey,
			const std::function<const struct MaterialPipelineBinding& (MaterialId, uint64)>& resolver,
			std::vector<DrawIndirectPacket>& outPackets) const;

		void BuildTerrainDrawPackets(
			uint64 passKey,
			const std::function<const struct MaterialPipelineBinding& (MaterialId, uint64)>& resolver,
			std::vector<DrawPacket>& outPackets) const;

		uint32 GetBatchCount() const noexcept { return static_cast<uint32>(m_Batches.size()); }

		struct BatchView final
		{
			const StaticMeshRenderData* pMesh = {};
			uint32 LodIndex = 0;
			uint32 SectionIndex = 0;
			MaterialId MaterialId = 0;

			bool bCastShadow = true;
			uint64 PassKey = 0;
		};

		bool TryGetBatchView(uint32 batchId, BatchView& outView) const noexcept;

		// ------------------------------------------------------------
		// Interaction stamps
		// ------------------------------------------------------------
		void AddInteractionStamp(const hlsl::InteractionStamp& stamp) { m_InteractionStamps.emplace_back(stamp); }
		void ConsumeInteractionStamps(std::vector<hlsl::InteractionStamp>* out) { out->swap(m_InteractionStamps); m_InteractionStamps.clear(); }

	private:
		static constexpr uint32 INVALID_INDEX = 0xFFFFFFFFu;

		template<typename T>
		struct Slot final
		{
			UniqueHandle<T> Owner = {};
			uint32 DenseIndex = INVALID_INDEX;
			bool bOccupied = false;
		};

		template<typename T>
		static void ensureCapacity(uint32 index, std::vector<Slot<T>>& v)
		{
			if (index >= static_cast<uint32>(v.size()))
			{
				v.resize(static_cast<size_t>(index) + 1024);
			}
		}

		static void ensureCapacity(uint32 index, std::vector<uint32>& v)
		{
			if (index >= static_cast<uint32>(v.size()))
			{
				v.resize(static_cast<size_t>(index) + 1024, INVALID_INDEX);
			}
		}

		template<typename T>
		uint32 findDenseIndex(Handle<T> h, const std::vector<Slot<T>>& slots) const noexcept;

		// ------------------------------------------------------------
		// Batch Key (mesh objects) - LOD SPECIFIC
		// ------------------------------------------------------------
		struct DrawBatchKey final
		{
			const void* MeshPtr = nullptr;
			uint32 LodIndex = 0;
			uint32 SectionIndex = 0;
			uint64 PassKey = 0;
			MaterialId MatId = 0;
			bool bCastShadow = true;

			bool operator==(const DrawBatchKey& rhs) const noexcept
			{
				return MeshPtr == rhs.MeshPtr
					&& LodIndex == rhs.LodIndex
					&& SectionIndex == rhs.SectionIndex
					&& PassKey == rhs.PassKey
					&& MatId == rhs.MatId
					&& bCastShadow == rhs.bCastShadow;
			}
		};

		struct DrawBatchKeyHasher final
		{
			size_t operator()(const DrawBatchKey& k) const noexcept
			{
				size_t h = reinterpret_cast<size_t>(k.MeshPtr);
				h ^= (static_cast<size_t>(k.LodIndex) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				h ^= (static_cast<size_t>(k.SectionIndex) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				h ^= (static_cast<size_t>(k.PassKey) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				h ^= (static_cast<size_t>(k.MatId) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				h ^= (static_cast<size_t>(k.bCastShadow) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				return h;
			}
		};

		struct SectionHandle final
		{
			uint32 BatchId = INVALID_INDEX;
			uint32 InstanceIndex = INVALID_INDEX;
		};

		struct SectionHandles final
		{
			SectionHandle Main = {};
			SectionHandle Shadow = {};
			SectionHandle Depth = {};
		};

		struct BatchInstance final
		{
			uint32 OcIndex = 0;
			uint32 OwnerObjectDenseIndex = 0;

			uint16 OwnerLodIndex = 0;
			uint16 OwnerSectionIndex = 0;

			uint8  OwnerPassSlot = 0; // 0:Main, 1:Shadow, 2:Depth
		};

		struct Batch final
		{
			DrawBatchKey Key = {};

			const StaticMeshRenderData* pMesh = nullptr;
			uint32 LodIndex = 0;
			uint32 SectionIndex = 0;
			MaterialId MaterialId = 0;
			uint64 PassKey = 0;
			bool bCastShadow = true;

			std::vector<BatchInstance> Instances;

			bool IsEmpty() const noexcept { return Instances.empty(); }
		};

		struct ObjectRecord final
		{
			SceneObject Obj = {};
			uint32 OcIndex = INVALID_INDEX;

			// [lod][section] -> handles for (Main/Shadow/Depth)
			std::vector<std::vector<SectionHandles>> SectionsByLod;
		};

	private:
		uint32 allocOcIndex();
		void freeOcIndex(uint32 ocIndex);
		void markOcDirty(uint32 ocIndex);

		uint64 classifyMainPassKey(MaterialId matId) const noexcept;
		bool   shouldRenderInShadow(MaterialId matId) const noexcept;

		uint32 getOrCreateBatch(
			const DrawBatchKey& key,
			const StaticMeshRenderData& mesh,
			uint32 lodIndex,
			uint32 sectionIndex,
			MaterialId matId,
			uint64 passKey,
			bool bCastShadow);

		void addObjectToBatches(uint32 objectDenseIndex);
		void removeObjectFromBatches(uint32 objectDenseIndex);
		void batchRemoveInstance(uint32 batchId, uint32 instanceIndex);

		static DrawBatchKey makeBatchKey(
			uint64 passKey,
			const StaticMeshRenderData& mesh,
			uint32 lodIndex,
			uint32 sectionIndex,
			MaterialId matId,
			bool bCastShadow);

	private:
		// Objects
		std::vector<Slot<SceneObject>> m_ObjectSlots;
		std::vector<uint32> m_ObjectSparse;
		std::vector<ObjectRecord> m_ObjectDense;
		std::vector<Handle<SceneObject>> m_ObjectHandles;

		// Terrain
		std::vector<Slot<TerrainObject>> m_TerrainSlots;
		std::vector<uint32> m_TerrainSparse;
		std::vector<TerrainObject> m_TerrainDense;
		std::vector<Handle<TerrainObject>> m_TerrainHandles;

		// Indirect
		std::vector<Slot<IndirectObject>> m_IndirectSlots;
		std::vector<uint32> m_IndirectSparse;
		std::vector<IndirectObject> m_IndirectDense;
		std::vector<Handle<IndirectObject>> m_IndirectHandles;

		// Lights
		std::vector<Slot<LightObject>> m_LightSlots;
		std::vector<uint32> m_LightSparse;
		std::vector<LightObject> m_LightDense;
		std::vector<Handle<LightObject>> m_LightHandles;

		//
		std::unordered_map<DrawBatchKey, uint32, DrawBatchKeyHasher> m_BatchLookup;
		std::vector<Batch> m_Batches;

		std::vector<hlsl::ObjectConstants> m_ObjectTableCPU;
		std::vector<uint32> m_FreeOcIndices;

		std::vector<uint8> m_OcDirty;
		std::vector<uint32> m_DirtyOcIndices;

		std::vector<hlsl::InteractionStamp> m_InteractionStamps;
	};
} // namespace shz