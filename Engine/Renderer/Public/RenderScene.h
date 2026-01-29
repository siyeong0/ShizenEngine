#pragma once
#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/Core/Math/Math.h"
#include "Engine/Renderer/Public/RenderData.h"

#include "Engine/RuntimeData/Public/TerrainHeightField.h"
#include "Engine/RuntimeData/Public/TerrainMeshBuilder.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	class RenderScene final
	{
	public:
		struct SceneObject final
		{
			const StaticMeshRenderData* pMesh = {};

			Matrix4x4 World = {};
			Matrix4x4 WorldInvTranspose = {};

			bool bCastShadow = true;
		};

		struct LightObject final
		{
			uint32 Type = 0; // TODO: replace with enum
			float3 Color = { 1.0f, 1.0f, 1.0f };
			float  Intensity = 1.0f;

			float3 Position = { 0.0f, 0.0f, 0.0f };
			float3 Direction = { 0.0f, -1.0f, 0.0f };

			float Range = 10.0f;
			float SpotAngle = 30.0f;

			bool CastShadow = false;
		};

		struct DrawItem final
		{
			uint32 BatchId = 0;
			uint32 StartInstanceLocation = 0;
			uint32 InstanceCount = 0;
		};

	public:
		RenderScene() = default;
		RenderScene(const RenderScene&) = delete;
		RenderScene& operator=(const RenderScene&) = delete;
		~RenderScene() = default;

		void Reset();

		// Scene Objects
		Handle<SceneObject> AddObject(const StaticMeshRenderData& rd, const Matrix4x4& transform, bool bCastShadow = true);
		void RemoveObject(Handle<SceneObject> h);
		void UpdateObjectMesh(Handle<SceneObject> h, const StaticMeshRenderData& mesh);
		void UpdateObjectTransform(Handle<SceneObject> h, const Matrix4x4& world);

		SceneObject* GetObjectOrNull(Handle<SceneObject> h) noexcept;
		const SceneObject* GetObjectOrNull(Handle<SceneObject> h) const noexcept;

		uint32 GetObjectCount() const noexcept { return static_cast<uint32>(m_ObjectDense.size()); }

		// Lights
		Handle<LightObject> AddLight(const LightObject& light);
		void RemoveLight(Handle<LightObject> h);
		void UpdateLight(Handle<LightObject> h, const LightObject& light);

		LightObject* GetLightOrNull(Handle<LightObject> h) noexcept;
		const LightObject* GetLightOrNull(Handle<LightObject> h) const noexcept;

		uint32 GetLightCount() const noexcept { return static_cast<uint32>(m_LightDense.size()); }
		const std::vector<LightObject>& GetLights() const noexcept { return m_LightDense; }

		// ------------------------------------------------------------
		// ObjectConstants Table (CPU mirror)
		// - Renderer는 dirty ranges만 GPU로 업로드하면 된다.
		// ------------------------------------------------------------
		const std::vector<hlsl::ObjectConstants>& GetObjectConstantsTableCPU() const noexcept { return m_ObjectTableCPU; }

		// dirty OcIndex 목록(중복 없음, 순서 보장 없음)
		const std::vector<uint32>& GetDirtyOcIndices() const noexcept { return m_DirtyOcIndices; }
		void ClearDirtyOcIndices();

		// Dense iteration for renderer (visibility, etc.)
		uint32 GetObjectDenseCount() const noexcept { return static_cast<uint32>(m_ObjectDense.size()); }

		// NOTE: Dense index is internal stable only within a frame.
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

		// Visible-aware draw list
		void BuildDrawList(
			uint64 passKey,
			const std::vector<uint32>& visibleObjectDenseIndices,
			std::vector<DrawItem>& outDrawItems,
			std::vector<uint32>& outInstanceRemap) const;


		// Renderer가 BatchId로 상태를 조회할 수 있게
		uint32 GetBatchCount() const noexcept { return static_cast<uint32>(m_Batches.size()); }

		struct BatchView final
		{
			const StaticMeshRenderData* pMesh = {};
			uint32 SectionIndex = 0;
			bool bCastShadow = true;
		};

		bool TryGetBatchView(uint32 batchId, BatchView& outView) const noexcept;

		// ------------------------------------------------------------
	   // Height field / Terrain
	   // - HeightMap은 렌더/시뮬/잔디생성 등에서 공용으로 쓰일 수 있으니 씬이 보관
	   // - TerrainMesh는 SceneObject로 추가되고, 핸들을 따로 들고 관리
	   // ------------------------------------------------------------
		void SetTerrain(const TextureRenderData& heightMap, const StaticMeshRenderData& terrainMesh, const Matrix4x4& world = Matrix4x4::Identity());
		void ClearTerrain();

		bool HasTerrain() const noexcept { return m_TerrainMesh.IsValid() && m_TerrainMesh.IsAlive(); }

		const TextureRenderData& GetHeightMap() const noexcept { return *m_pTerrainHeightMap; }
		const Handle<SceneObject>& GetTerrainMeshHandle() const noexcept { return m_TerrainMesh; }
		void AddInteractionStamp(const hlsl::InteractionStamp& stamp) { m_InteractionStamps.emplace_back(stamp); }
		void ConsumeInteractionStamps(std::vector<hlsl::InteractionStamp>* out) { out->swap(m_InteractionStamps); m_InteractionStamps.clear(); }

	private:
		static constexpr uint32 INVALID_INDEX = 0xFFFFFFFFu;

		// Public Handle -> internal slot mapping
		template<typename T>
		struct Slot final
		{
			UniqueHandle<T> Owner = {}; // owns handle lifetime
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
		// Batch Key
		//
		// 핵심: "드로우콜을 나누는 기준"을 모두 포함해야 한다.
		// - 실제로는 PSO/SRB/Geom/IndexRange/Pass 등을 ID로 축약해 넣는 게 좋다.
		//
		// 여기서는 엔진 내부 구조를 모르므로:
		// - 섹션을 대표하는 (Mesh, SectionIndex, bCastShadow, Pass)로 키를 구성한다.
		// - 추후 Material/PSO/SRB 캐시 ID가 있다면 그걸로 축약하도록 교체하면 됨.
		// ------------------------------------------------------------
		struct DrawBatchKey final
		{
			const void* MeshPtr = nullptr; // StaticMeshRenderData의 주소(동일 renderdata면 동일 포인터 기대)
			uint32 SectionIndex = 0;
			uint64 PassKey = 0;          // ERenderPass
			bool   bCastShadow = true;

			bool operator==(const DrawBatchKey& rhs) const noexcept
			{
				return MeshPtr == rhs.MeshPtr
					&& SectionIndex == rhs.SectionIndex
					&& PassKey == rhs.PassKey
					&& bCastShadow == rhs.bCastShadow;
			}
		};

		struct DrawBatchKeyHasher final
		{
			size_t operator()(const DrawBatchKey& k) const noexcept
			{
				// simple mix
				size_t h = reinterpret_cast<size_t>(k.MeshPtr);
				h ^= (static_cast<size_t>(k.SectionIndex) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				h ^= (static_cast<size_t>(k.PassKey) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				h ^= (static_cast<size_t>(k.bCastShadow) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
				return h;
			}
		};

		// ------------------------------------------------------------
		// Batch / Instances
		// ------------------------------------------------------------
		struct SectionHandle final
		{
			uint32 BatchId = INVALID_INDEX;
			uint32 InstanceIndex = INVALID_INDEX;
		};

		struct BatchInstance final
		{
			// Remap을 통해 결국 ObjectTable[OcIndex]를 참조한다.
			uint32 OcIndex = 0;

			// swap-remove 시 owner의 SectionHandle을 갱신하기 위한 역참조
			uint32 OwnerObjectDenseIndex = 0;
			uint16 OwnerSectionSlot = 0;
		};

		struct Batch final
		{
			DrawBatchKey Key = {};

			// Renderer가 해석할 참조(최소 구현)
			const StaticMeshRenderData* pMesh = nullptr;
			uint32 SectionIndex = 0;
			bool bCastShadow = true;

			std::vector<BatchInstance> Instances;

			bool IsEmpty() const noexcept { return Instances.empty(); }
		};

		// ------------------------------------------------------------
		// Object storage
		// ------------------------------------------------------------
		struct ObjectRecord final
		{
			SceneObject Obj = {};

			// 고정 OcIndex 슬롯
			uint32 OcIndex = INVALID_INDEX;

			// 이 오브젝트가 삽입된 섹션 핸들들(섹션 수와 동일)
			std::vector<SectionHandle> Sections;
		};

	private:
		// OcIndex allocator
		uint32 allocOcIndex();
		void freeOcIndex(uint32 ocIndex);

		void markOcDirty(uint32 ocIndex);

		// Batch ops
		uint32 getOrCreateBatch(const DrawBatchKey& key, const StaticMeshRenderData& mesh, uint32 sectionIndex, bool bCastShadow);
		void addObjectToBatches(uint32 objectDenseIndex);
		void removeObjectFromBatches(uint32 objectDenseIndex);

		void batchRemoveInstance(uint32 batchId, uint32 instanceIndex);

		static DrawBatchKey makeBatchKey(uint64 passKey, const StaticMeshRenderData& mesh, uint32 sectionIndex, bool bCastShadow);

	private:
		// ------------------------------------------------------------
		// Objects: Dense/Sparse (public handle)
		// ------------------------------------------------------------
		std::vector<Slot<SceneObject>> m_ObjectSlots;
		std::vector<uint32> m_ObjectSparse;
		std::vector<ObjectRecord> m_ObjectDense;
		std::vector<Handle<SceneObject>> m_ObjectHandles;

		// ------------------------------------------------------------
		// Lights: Dense/Sparse
		// ------------------------------------------------------------
		std::vector<Slot<LightObject>> m_LightSlots;
		std::vector<uint32> m_LightSparse;
		std::vector<LightObject> m_LightDense;
		std::vector<Handle<LightObject>> m_LightHandles;

		// ------------------------------------------------------------
		// Batches
		// ------------------------------------------------------------
		std::unordered_map<DrawBatchKey, uint32, DrawBatchKeyHasher> m_BatchLookup;
		std::vector<Batch> m_Batches;

		// ------------------------------------------------------------
		// ObjectConstants table
		// ------------------------------------------------------------
		std::vector<hlsl::ObjectConstants> m_ObjectTableCPU;
		std::vector<uint32> m_FreeOcIndices;

		// dirty tracking
		std::vector<uint8> m_OcDirty;          // OcIndex -> 0/1
		std::vector<uint32> m_DirtyOcIndices;  // unique list

		// ------------------------------------------------------------
		// Terrain / Height field
		// ------------------------------------------------------------
		const TextureRenderData* m_pTerrainHeightMap = {};
		Handle<SceneObject> m_TerrainMesh = {};

		std::vector<hlsl::InteractionStamp> m_InteractionStamps;
	};
} // namespace shz
