#include "pch.h"
#include "RenderScene.h"

namespace shz
{
	// ------------------------------------------------------------
	// Reset
	// ------------------------------------------------------------
	void RenderScene::Reset()
	{
		for (Slot<SceneObject>& s : m_ObjectSlots)
		{
			s.Owner.Reset();
			s.DenseIndex = INVALID_INDEX;
			s.bOccupied = false;
		}
		for (Slot<LightObject>& s : m_LightSlots)
		{
			s.Owner.Reset();
			s.DenseIndex = INVALID_INDEX;
			s.bOccupied = false;
		}

		m_ObjectSparse.clear();
		m_LightSparse.clear();

		m_ObjectDense.clear();
		m_ObjectHandles.clear();

		m_LightDense.clear();
		m_LightHandles.clear();

		m_BatchLookup.clear();
		m_Batches.clear();

		m_ObjectTableCPU.clear();
		m_FreeOcIndices.clear();
		m_OcDirty.clear();
		m_DirtyOcIndices.clear();

		m_pTerrainHeightMap = nullptr;
		m_TerrainMesh = {};
	}

	void RenderScene::ClearDirtyOcIndices()
	{
		for (uint32 oc : m_DirtyOcIndices)
		{
			ASSERT(oc < static_cast<uint32>(m_OcDirty.size()), "Object constant index out of bounds.");
			m_OcDirty[oc] = 0;
		}
		m_DirtyOcIndices.clear();
	}

	// ------------------------------------------------------------
	// Scene Objects API
	// ------------------------------------------------------------
	Handle<RenderScene::SceneObject> RenderScene::AddObject(const StaticMeshRenderData& rd, const Matrix4x4& transform, bool bCastShadow)
	{
		UniqueHandle<SceneObject> owner = UniqueHandle<SceneObject>::Make();
		const Handle<SceneObject> h = owner.Get();
		ASSERT(h.IsValid(), "Failed to allocate SceneObject handle.");

		const uint32 handleIndex = h.GetIndex();
		ensureCapacity(handleIndex, m_ObjectSlots);
		ensureCapacity(handleIndex, m_ObjectSparse);

		Slot<SceneObject>& slot = m_ObjectSlots[handleIndex];
		ASSERT(!slot.bOccupied && !slot.Owner.Get().IsValid(), "SceneObject slot already occupied.");

		const uint32 denseIndex = static_cast<uint32>(m_ObjectDense.size());

		ObjectRecord rec = {};
		rec.Obj.pMesh = &rd;
		rec.Obj.World = transform;
		rec.Obj.WorldInvTranspose = transform.Inversed().Transposed();
		rec.Obj.bCastShadow = bCastShadow;

		rec.OcIndex = allocOcIndex();

		// Dense store
		m_ObjectDense.emplace_back(std::move(rec));
		m_ObjectHandles.emplace_back(h);

		// Bind slot/sparse
		slot.Owner = std::move(owner);
		slot.DenseIndex = denseIndex;
		slot.bOccupied = true;

		m_ObjectSparse[handleIndex] = denseIndex;

		// Insert into batches (section split happens here)
		addObjectToBatches(denseIndex);

		return h;
	}

	void RenderScene::RemoveObject(Handle<SceneObject> h)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to remove non-existing SceneObject.");

		ASSERT(denseIndex < static_cast<uint32>(m_ObjectDense.size()), "Dense index out of range.");
		ASSERT(m_ObjectHandles[denseIndex] == h, "Dense handle mismatch (internal corruption).");

		// 1) remove from batches & free OcIndex
		{
			ObjectRecord& rec = m_ObjectDense[denseIndex];
			removeObjectFromBatches(denseIndex);
			freeOcIndex(rec.OcIndex);
			rec.OcIndex = INVALID_INDEX;
		}

		// 2) dense swap-remove
		const uint32 lastIndex = static_cast<uint32>(m_ObjectDense.size() - 1);
		if (denseIndex != lastIndex)
		{
			// move last -> removed spot
			m_ObjectDense[denseIndex] = std::move(m_ObjectDense[lastIndex]);

			const Handle<SceneObject> movedHandle = m_ObjectHandles[lastIndex];
			m_ObjectHandles[denseIndex] = movedHandle;

			// fix moved handle slot + sparse
			const uint32 movedHandleIndex = movedHandle.GetIndex();
			ASSERT(movedHandleIndex < static_cast<uint32>(m_ObjectSlots.size()), "Moved handle slot missing.");
			Slot<SceneObject>& movedSlot = m_ObjectSlots[movedHandleIndex];
			ASSERT(movedSlot.bOccupied && movedSlot.Owner.Get() == movedHandle, "Moved slot mismatch.");
			movedSlot.DenseIndex = denseIndex;

			ASSERT(movedHandleIndex < static_cast<uint32>(m_ObjectSparse.size()), "Moved sparse missing.");
			m_ObjectSparse[movedHandleIndex] = denseIndex;

			// IMPORTANT:
			// moved object가 이미 batches에 들어있던 인스턴스들은 OwnerObjectDenseIndex가 "lastIndex"로 되어있음.
			// denseIndex로 바뀌었으니 그 역참조를 모두 갱신해야 한다.
			// (remove가 자주 일어나지 않는다는 전제에서, 각 섹션 핸들로 O(sections) 갱신)
			ObjectRecord& movedRec = m_ObjectDense[denseIndex];
			for (uint32 si = 0; si < static_cast<uint32>(movedRec.Sections.size()); ++si)
			{
				const SectionHandle& sh = movedRec.Sections[si];
				if (sh.BatchId == INVALID_INDEX || sh.InstanceIndex == INVALID_INDEX)
				{
					continue;
				}

				ASSERT(sh.BatchId < static_cast<uint32>(m_Batches.size()), "Moved object: batchId OOB.");
				Batch& b = m_Batches[sh.BatchId];
				ASSERT(sh.InstanceIndex < static_cast<uint32>(b.Instances.size()), "Moved object: instanceIndex OOB.");

				BatchInstance& inst = b.Instances[sh.InstanceIndex];
				inst.OwnerObjectDenseIndex = denseIndex;
			}

			// Shadow pass 인스턴스도 OwnerObjectDenseIndex를 갱신해야 하는데,
			// 여기서는 remove가 드물다는 가정으로 "shadow 배치 전체를 선형 스캔"해서 갱신한다.
			// (최적화: shadow도 pass별 SectionHandle로 저장해 O(sections) 갱신)
			for (Batch& b : m_Batches)
			{
				if (b.Key.PassKey != STRING_HASH("Shadow"))
				{
					continue;
				}

				for (BatchInstance& inst : b.Instances)
				{
					if (inst.OwnerObjectDenseIndex == lastIndex)
					{
						inst.OwnerObjectDenseIndex = denseIndex;
					}
				}
			}
		}

		m_ObjectDense.pop_back();
		m_ObjectHandles.pop_back();

		// 3) clear handle slot/sparse
		const uint32 handleIndex = h.GetIndex();
		ASSERT(handleIndex < static_cast<uint32>(m_ObjectSlots.size()), "Handle slot missing.");
		Slot<SceneObject>& slot = m_ObjectSlots[handleIndex];
		slot.Owner.Reset();
		slot.DenseIndex = INVALID_INDEX;
		slot.bOccupied = false;

		if (handleIndex < static_cast<uint32>(m_ObjectSparse.size()))
		{
			m_ObjectSparse[handleIndex] = INVALID_INDEX;
		}
	}

	void RenderScene::UpdateObjectMesh(Handle<SceneObject> h, const StaticMeshRenderData& mesh)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to update non-existing SceneObject.");

		ObjectRecord& rec = m_ObjectDense[denseIndex];

		// Safest approach: remove from the existing batch -> replace the mesh -> reinsert into the batch
		removeObjectFromBatches(denseIndex);
		rec.Obj.pMesh = &mesh;
		addObjectToBatches(denseIndex);
	}

	void RenderScene::UpdateObjectTransform(Handle<SceneObject> h, const Matrix4x4& world)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to update non-existing SceneObject.");

		ObjectRecord& rec = m_ObjectDense[denseIndex];
		rec.Obj.World = world;
		rec.Obj.WorldInvTranspose = world.Inversed().Transposed();

		ASSERT(rec.OcIndex != INVALID_INDEX, "Object has no OcIndex.");
		m_ObjectTableCPU[rec.OcIndex].World = rec.Obj.World;
		m_ObjectTableCPU[rec.OcIndex].WorldInvTranspose = rec.Obj.WorldInvTranspose;
		markOcDirty(rec.OcIndex);
	}

	RenderScene::SceneObject* RenderScene::GetObjectOrNull(Handle<SceneObject> h) noexcept
	{
		const uint32 dense = findDenseIndex(h, m_ObjectSlots);
		if (dense == INVALID_INDEX)
		{
			return nullptr;
		}
		return &m_ObjectDense[(size_t)dense].Obj;
	}

	const RenderScene::SceneObject* RenderScene::GetObjectOrNull(Handle<SceneObject> h) const noexcept
	{
		const uint32 dense = findDenseIndex(h, m_ObjectSlots);
		if (dense == INVALID_INDEX)
		{
			return nullptr;
		}
		return &m_ObjectDense[(size_t)dense].Obj;
	}

	// ------------------------------------------------------------
	// Lights 
	// ------------------------------------------------------------
	Handle<RenderScene::LightObject> RenderScene::AddLight(const LightObject& light)
	{
		UniqueHandle<LightObject> owner = UniqueHandle<LightObject>::Make();
		const Handle<LightObject> h = owner.Get();
		ASSERT(h.IsValid(), "Failed to allocate LightObject handle.");

		const uint32 handleIndex = h.GetIndex();
		ensureCapacity(handleIndex, m_LightSlots);
		ensureCapacity(handleIndex, m_LightSparse);

		Slot<LightObject>& slot = m_LightSlots[handleIndex];
		ASSERT(!slot.bOccupied && !slot.Owner.Get().IsValid(), "LightObject slot already occupied.");

		const uint32 denseIndex = static_cast<uint32>(m_LightDense.size());

		m_LightDense.push_back(light);
		m_LightHandles.push_back(h);

		slot.Owner = std::move(owner);
		slot.DenseIndex = denseIndex;
		slot.bOccupied = true;

		m_LightSparse[handleIndex] = denseIndex;

		return h;
	}

	void RenderScene::RemoveLight(Handle<LightObject> h)
	{
		const uint32 denseIndex = findDenseIndex(h, m_LightSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to remove non-existing LightObject.");

		ASSERT(denseIndex < static_cast<uint32>(m_LightDense.size()), "Dense index out of range.");
		ASSERT(m_LightHandles[denseIndex] == h, "Dense handle mismatch (internal corruption).");

		const uint32 lastIndex = static_cast<uint32>(m_LightDense.size() - 1);
		if (denseIndex != lastIndex)
		{
			m_LightDense[denseIndex] = std::move(m_LightDense[lastIndex]);

			const Handle<LightObject> movedHandle = m_LightHandles[lastIndex];
			m_LightHandles[denseIndex] = movedHandle;

			const uint32 movedHandleIndex = movedHandle.GetIndex();
			ASSERT(movedHandleIndex < static_cast<uint32>(m_LightSlots.size()), "Moved handle slot missing.");
			Slot<LightObject>& movedSlot = m_LightSlots[movedHandleIndex];
			ASSERT(movedSlot.bOccupied && movedSlot.Owner.Get() == movedHandle, "Moved slot mismatch.");
			movedSlot.DenseIndex = denseIndex;

			ASSERT(movedHandleIndex < static_cast<uint32>(m_LightSparse.size()), "Moved sparse missing.");
			m_LightSparse[movedHandleIndex] = denseIndex;
		}

		m_LightDense.pop_back();
		m_LightHandles.pop_back();

		const uint32 handleIndex = h.GetIndex();
		ASSERT(handleIndex < static_cast<uint32>(m_LightSlots.size()), "Handle slot missing.");
		Slot<LightObject>& slot = m_LightSlots[handleIndex];
		slot.Owner.Reset();
		slot.DenseIndex = INVALID_INDEX;
		slot.bOccupied = false;

		if (handleIndex < static_cast<uint32>(m_LightSparse.size()))
		{
			m_LightSparse[handleIndex] = INVALID_INDEX;
		}
	}

	void RenderScene::UpdateLight(Handle<LightObject> h, const LightObject& light)
	{
		const uint32 denseIndex = findDenseIndex(h, m_LightSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to update non-existing LightObject.");

		m_LightDense[denseIndex] = light;
	}

	RenderScene::LightObject* RenderScene::GetLightOrNull(Handle<LightObject> h) noexcept
	{
		const uint32 dense = findDenseIndex(h, m_LightSlots);
		if (dense == INVALID_INDEX) return nullptr;
		return &m_LightDense[(size_t)dense];
	}

	const RenderScene::LightObject* RenderScene::GetLightOrNull(Handle<LightObject> h) const noexcept
	{
		const uint32 dense = findDenseIndex(h, m_LightSlots);
		if (dense == INVALID_INDEX) return nullptr;
		return &m_LightDense[(size_t)dense];
	}

	// ------------------------------------------------------------
	// Draw list build
	// ------------------------------------------------------------
	void RenderScene::BuildDrawList(
		uint64 passKey,
		const std::vector<uint32>& visibleObjectDenseIndices,
		std::vector<DrawItem>& outDrawItems,
		std::vector<uint32>& outInstanceRemap) const
	{
		outDrawItems.clear();
		outInstanceRemap.clear();

		if (visibleObjectDenseIndices.empty())
		{
			return;
		}

		// OcIndex visibility mask
		std::vector<uint8> ocVisible;
		ocVisible.resize(m_ObjectTableCPU.size(), 0);

		for (uint32 objDense : visibleObjectDenseIndices)
		{
			ASSERT(objDense < static_cast<uint32>(m_ObjectDense.size()), "Object dense index out of bounds.");

			const uint32 oc = m_ObjectDense[objDense].OcIndex;
			ASSERT(oc != INVALID_INDEX && oc < static_cast<uint32>(ocVisible.size()), "Invalid object constant index.");
			ocVisible[oc] = 1;
		}

		// Iterate batches -> select instances whose OcIndex is visible
		for (uint32 batchId = 0; batchId < static_cast<uint32>(m_Batches.size()); ++batchId)
		{
			const Batch& b = m_Batches[batchId];
			if (b.IsEmpty())
			{
				continue;
			}

			if (b.Key.PassKey != passKey)
			{
				continue;
			}

			// Shadow pass: skip objects that do not cast shadows
			// (Note: if batches are already partitioned by bCastShadow,
			//  this check remains valid.)
			// if (passKey == kPassShadow && !b.bCastShadow) continue;

			const uint32 start = static_cast<uint32>(outInstanceRemap.size());
			uint32 count = 0;

			for (const BatchInstance& inst : b.Instances)
			{
				const uint32 oc = inst.OcIndex;
				ASSERT(oc < static_cast<uint32>(ocVisible.size()), "Object constant index out of bounds.");
				if (ocVisible[oc])
				{
					outInstanceRemap.push_back(oc);
					++count;
				}
			}

			if (count == 0)
			{
				continue; // If nothing is visible, do not draw this batch
			}

			DrawItem di = {};
			di.BatchId = batchId;
			di.StartInstanceLocation = start;
			di.InstanceCount = count;
			outDrawItems.emplace_back(di);
		}
	}


	bool RenderScene::TryGetBatchView(uint32 batchId, BatchView& outView) const noexcept
	{
		ASSERT(batchId < static_cast<uint32>(m_Batches.size()), "Batch ID out of bounds.");

		const Batch& b = m_Batches[batchId];
		outView.pMesh = b.pMesh;
		outView.SectionIndex = b.SectionIndex;
		outView.bCastShadow = b.bCastShadow;
		return true;
	}

	void RenderScene::SetTerrain(const TextureRenderData& heightMap, const StaticMeshRenderData& terrainMesh, const Matrix4x4& world)
	{
		// Remove the existing terrain if present
		ClearTerrain();

		m_pTerrainHeightMap = &heightMap;
		m_TerrainMesh = AddObject(terrainMesh, world, /*bCastShadow=*/true);
	}

	void RenderScene::ClearTerrain()
	{
		if (m_TerrainMesh.IsValid() && m_TerrainMesh.IsAlive())
		{
			RemoveObject(m_TerrainMesh);
			m_TerrainMesh = {};
		}

		m_pTerrainHeightMap = nullptr;
	}

	// ------------------------------------------------------------
	// OcIndex allocator / dirty
	// ------------------------------------------------------------
	uint32 RenderScene::allocOcIndex()
	{
		uint32 idx = INVALID_INDEX;

		if (!m_FreeOcIndices.empty())
		{
			idx = m_FreeOcIndices.back();
			m_FreeOcIndices.pop_back();
		}
		else
		{
			idx = static_cast<uint32>(m_ObjectTableCPU.size());
			m_ObjectTableCPU.emplace_back(hlsl::ObjectConstants{});
			m_OcDirty.emplace_back(0);
		}

		ASSERT(idx != INVALID_INDEX, "allocOcIndex failed.");
		return idx;
	}

	void RenderScene::freeOcIndex(uint32 ocIndex)
	{
		if (ocIndex == INVALID_INDEX)
		{
			return;
		}

		ASSERT(ocIndex < static_cast<uint32>(m_ObjectTableCPU.size()), "freeOcIndex out of range.");
		m_FreeOcIndices.push_back(ocIndex);

		// dirty flag는 남겨도 되지만, 안전을 위해 0으로.
		if (ocIndex < static_cast<uint32>(m_OcDirty.size()))
		{
			m_OcDirty[ocIndex] = 0;
		}
	}

	void RenderScene::markOcDirty(uint32 ocIndex)
	{
		ASSERT(ocIndex != INVALID_INDEX, "markOcDirty called with invalid OcIndex.");
		ASSERT(ocIndex < static_cast<uint32>(m_OcDirty.size()), "markOcDirty out of range.");

		if (m_OcDirty[ocIndex] == 0)
		{
			m_OcDirty[ocIndex] = 1;
			m_DirtyOcIndices.push_back(ocIndex);
		}
	}

	// ------------------------------------------------------------
	// Batch key
	// ------------------------------------------------------------
	RenderScene::DrawBatchKey RenderScene::makeBatchKey(uint64 passKey, const StaticMeshRenderData& mesh, uint32 sectionIndex, bool bCastShadow)
	{
		DrawBatchKey k = {};
		k.MeshPtr = &mesh;
		k.SectionIndex = sectionIndex;
		k.PassKey = passKey;

		k.bCastShadow = bCastShadow;
		return k;
	}

	uint32 RenderScene::getOrCreateBatch(const DrawBatchKey& key, const StaticMeshRenderData& mesh, uint32 sectionIndex, bool bCastShadow)
	{
		auto it = m_BatchLookup.find(key);
		if (it != m_BatchLookup.end())
		{
			return it->second;
		}

		const uint32 batchId = static_cast<uint32>(m_Batches.size());

		Batch b = {};
		b.Key = key;
		b.pMesh = &mesh;
		b.SectionIndex = sectionIndex;
		b.bCastShadow = bCastShadow;

		m_Batches.emplace_back(std::move(b));
		m_BatchLookup.emplace(key, batchId);
		return batchId;
	}

	void RenderScene::batchRemoveInstance(uint32 batchId, uint32 instanceIndex)
	{
		ASSERT(batchId < static_cast<uint32>(m_Batches.size()), "batchRemoveInstance: batchId out of range.");
		Batch& batch = m_Batches[batchId];
		ASSERT(instanceIndex < static_cast<uint32>(batch.Instances.size()), "batchRemoveInstance: instanceIndex out of range.");

		const uint32 lastIndex = static_cast<uint32>(batch.Instances.size() - 1);
		if (instanceIndex != lastIndex)
		{
			// swap-remove
			BatchInstance moved = batch.Instances[lastIndex];
			batch.Instances[instanceIndex] = moved;

			// moved instance의 owner section handle 갱신
			ASSERT(moved.OwnerObjectDenseIndex < static_cast<uint32>(m_ObjectDense.size()), "batchRemoveInstance: moved owner out of range.");
			ObjectRecord& movedOwner = m_ObjectDense[moved.OwnerObjectDenseIndex];
			ASSERT(moved.OwnerSectionSlot < movedOwner.Sections.size(), "batchRemoveInstance: moved owner section slot out of range.");

			SectionHandle& movedHandle = movedOwner.Sections[moved.OwnerSectionSlot];
			ASSERT(movedHandle.BatchId == batchId, "batchRemoveInstance: moved handle batch mismatch.");
			ASSERT(movedHandle.InstanceIndex == lastIndex, "batchRemoveInstance: moved handle instance mismatch.");
			movedHandle.InstanceIndex = instanceIndex;
		}

		batch.Instances.pop_back();
	}

	// ------------------------------------------------------------
	// Object <-> batches
	// ------------------------------------------------------------
	void RenderScene::addObjectToBatches(uint32 objectDenseIndex)
	{
		ASSERT(objectDenseIndex < static_cast<uint32>(m_ObjectDense.size()), "addObjectToBatches: objectDenseIndex OOB.");

		ObjectRecord& rec = m_ObjectDense[objectDenseIndex];
		SceneObject& obj = rec.Obj;

		// ObjectConstants CPU mirror update (최초 1회)
		ASSERT(rec.OcIndex != INVALID_INDEX, "Object has no OcIndex.");
		ASSERT(rec.OcIndex < static_cast<uint32>(m_ObjectTableCPU.size()), "OcIndex OOB.");

		m_ObjectTableCPU[rec.OcIndex].World = obj.World;
		m_ObjectTableCPU[rec.OcIndex].WorldInvTranspose = obj.WorldInvTranspose;
		markOcDirty(rec.OcIndex);

		const uint32 sectionCount = static_cast<uint32>(obj.pMesh->Sections.size());

		rec.Sections.clear();
		rec.Sections.resize(sectionCount);

		for (uint32 si = 0; si < sectionCount; ++si)
		{
			// Pass별로 배치가 분리될 수 있다.
			// 여기서는 Main/Shadow 둘 다 만든다. (Shadow는 bCastShadow가 false면 건너뜀)
			{

				BatchInstance inst = {};
				inst.OcIndex = rec.OcIndex;
				inst.OwnerObjectDenseIndex = objectDenseIndex;
				inst.OwnerSectionSlot = static_cast<uint16>(si);

				const DrawBatchKey key = makeBatchKey(obj.pMesh->Sections[si].pMaterial->RenderPassId, *obj.pMesh, si, obj.bCastShadow);
				const uint32 batchId = getOrCreateBatch(key, *obj.pMesh, si, obj.bCastShadow);

				Batch& batch = m_Batches[batchId];
				const uint32 instIndex = static_cast<uint32>(batch.Instances.size());

				batch.Instances.emplace_back(inst);

				// 이 섹션 슬롯은 "Main pass 배치"를 기본으로 기록한다.
				// Shadow pass까지 별도 핸들이 필요하면 SectionHandle을 pass별로 나누는 구조로 확장하면 된다.
				rec.Sections[si].BatchId = batchId;
				rec.Sections[si].InstanceIndex = instIndex;
			}

			if (obj.bCastShadow)
			{
				const DrawBatchKey key = makeBatchKey(STRING_HASH("Shadow"), * obj.pMesh, si, obj.bCastShadow);
				(void)getOrCreateBatch(key, *obj.pMesh, si, obj.bCastShadow);

				// Shadow pass는 DrawList 빌드 시 따로 인스턴스를 순회해도 되고,
				// 배치를 pass별로 분리해서 여기서도 Instances를 추가해도 된다.
				//
				// "완전 단순"을 위해: Shadow 배치에도 동일 인스턴스를 넣는다.
				const uint32 shadowBatchId = m_BatchLookup[key];
				Batch& sb = m_Batches[shadowBatchId];
				const uint32 instIndex = static_cast<uint32>(sb.Instances.size());

				BatchInstance inst = {};
				inst.OcIndex = rec.OcIndex;
				inst.OwnerObjectDenseIndex = objectDenseIndex;
				inst.OwnerSectionSlot = static_cast<uint16>(si);

				sb.Instances.emplace_back(inst);
			}
		}
	}

	void RenderScene::removeObjectFromBatches(uint32 objectDenseIndex)
	{
		ASSERT(objectDenseIndex < static_cast<uint32>(m_ObjectDense.size()), "removeObjectFromBatches: objectDenseIndex OOB.");

		ObjectRecord& rec = m_ObjectDense[objectDenseIndex];

		// Main-pass SectionHandle 기반으로 제거 (Shadow 배치는 여기서 직접 탐색/제거)
		// 더 깔끔하게 하려면 pass별 SectionHandle을 별도로 저장하는 구조로 확장 추천.
		for (uint32 si = 0; si < static_cast<uint32>(rec.Sections.size()); ++si)
		{
			const SectionHandle sh = rec.Sections[si];
			if (sh.BatchId != INVALID_INDEX && sh.InstanceIndex != INVALID_INDEX)
			{
				batchRemoveInstance(sh.BatchId, sh.InstanceIndex);
			}

			// Shadow 배치 제거: 동일 key로 찾아 제거
			// (섹션 핸들을 pass별로 저장하면 이 부분이 O(1)로 깔끔해짐)
			{
				const DrawBatchKey skey = makeBatchKey(STRING_HASH("Shadow"), * rec.Obj.pMesh, si, rec.Obj.bCastShadow);
				auto it = m_BatchLookup.find(skey);
				if (it != m_BatchLookup.end())
				{
					const uint32 sbId = it->second;
					Batch& sb = m_Batches[sbId];

					// sb.Instances에서 (OwnerObjectDenseIndex, OwnerSectionSlot==si) 찾기
					// NOTE: 이 선형 탐색은 "object remove가 드물다" 전제에서 OK.
					// 제거가 빈번하면 shadow도 pass별 SectionHandle로 저장해라.
					for (uint32 ii = 0; ii < static_cast<uint32>(sb.Instances.size()); ++ii)
					{
						const BatchInstance& inst = sb.Instances[ii];
						if (inst.OwnerObjectDenseIndex == objectDenseIndex && inst.OwnerSectionSlot == static_cast<uint16>(si))
						{
							batchRemoveInstance(sbId, ii);
							break;
						}
					}
				}
			}
		}

		rec.Sections.clear();
	}

	// ------------------------------------------------------------
	// Common lookup
	// ------------------------------------------------------------
	template<typename T>
	uint32 RenderScene::findDenseIndex(Handle<T> h, const std::vector<Slot<T>>& slots) const noexcept
	{
		ASSERT(h.IsValid(), "findDenseIndex() called with invalid handle.");
		ASSERT(h.IsAlive(), "findDenseIndex() called with dead handle.");

		const uint32 idx = h.GetIndex();
		ASSERT(idx != 0, "findDenseIndex() called with invalid handle index.");
		ASSERT(idx < static_cast<uint32>(slots.size()), "findDenseIndex() called with out-of-bounds handle index.");

		const Slot<T>& slot = slots[idx];
		if (!slot.bOccupied)
		{
			return INVALID_INDEX;
		}

		// CRITICAL: ensure this handle matches the slot owner (index + generation)
		if (slot.Owner.Get() != h)
		{
			return INVALID_INDEX;
		}

		return slot.DenseIndex;
	}

} // namespace shz
