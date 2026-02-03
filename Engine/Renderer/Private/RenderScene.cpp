#include "pch.h"
#include "RenderScene.h"

#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

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
		for (Slot<IndirectObject>& s : m_IndirectSlots)
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
		m_IndirectSparse.clear();
		m_LightSparse.clear();

		m_ObjectDense.clear();
		m_ObjectHandles.clear();

		m_IndirectDense.clear();
		m_IndirectHandles.clear();

		m_LightDense.clear();
		m_LightHandles.clear();

		m_BatchLookup.clear();
		m_Batches.clear();

		m_ObjectTableCPU.clear();
		m_FreeOcIndices.clear();
		m_OcDirty.clear();
		m_DirtyOcIndices.clear();

		m_pTerrainHeightMap = {};
		m_TerrainMesh = {};

		m_InteractionStamps.clear();
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
	// Material -> Pass classification
	// ------------------------------------------------------------
	uint64 RenderScene::classifyMainPassKey(MaterialId matId) const noexcept
	{
		const Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);

		// Simple policy:
		// - Opaque/Masked => GBuffer
		// - Transparent  => Forward
		switch (mat.GetBlendMode())
		{
		case MATERIAL_BLEND_MODE_OPAQUE:
		case MATERIAL_BLEND_MODE_MASKED:
			return STRING_HASH("GBuffer");
		case MATERIAL_BLEND_MODE_TRANSPARENT:
			return STRING_HASH("Forward");
		default:
			ASSERT(false, "Invalid blend mode.");
		}
		ASSERT(false, "Failed to classify pass key.");
		return 0;
	}

	bool RenderScene::shouldRenderInShadow(MaterialId matId) const noexcept
	{
		const Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);

		// Common policy: transparent materials typically do not cast shadow.
		// Masked can cast shadow.
		return (mat.GetBlendMode() != MATERIAL_BLEND_MODE_TRANSPARENT);
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
			m_ObjectDense[denseIndex] = std::move(m_ObjectDense[lastIndex]);

			const Handle<SceneObject> movedHandle = m_ObjectHandles[lastIndex];
			m_ObjectHandles[denseIndex] = movedHandle;

			const uint32 movedHandleIndex = movedHandle.GetIndex();
			ASSERT(movedHandleIndex < static_cast<uint32>(m_ObjectSlots.size()), "Moved handle slot missing.");
			Slot<SceneObject>& movedSlot = m_ObjectSlots[movedHandleIndex];
			ASSERT(movedSlot.bOccupied && movedSlot.Owner.Get() == movedHandle, "Moved slot mismatch.");
			movedSlot.DenseIndex = denseIndex;

			ASSERT(movedHandleIndex < static_cast<uint32>(m_ObjectSparse.size()), "Moved sparse missing.");
			m_ObjectSparse[movedHandleIndex] = denseIndex;

			// Fix reverse references for moved object's instances (main + shadow handles)
			ObjectRecord& movedRec = m_ObjectDense[denseIndex];
			for (uint32 si = 0; si < static_cast<uint32>(movedRec.Sections.size()); ++si)
			{
				const SectionHandles& shs = movedRec.Sections[si];

				// Main
				if (shs.Main.BatchId != INVALID_INDEX && shs.Main.InstanceIndex != INVALID_INDEX)
				{
					ASSERT(shs.Main.BatchId < static_cast<uint32>(m_Batches.size()), "Moved object: main batchId OOB.");
					Batch& b = m_Batches[shs.Main.BatchId];
					ASSERT(shs.Main.InstanceIndex < static_cast<uint32>(b.Instances.size()), "Moved object: main instanceIndex OOB.");
					b.Instances[shs.Main.InstanceIndex].OwnerObjectDenseIndex = denseIndex;
				}

				// Shadow
				if (shs.Shadow.BatchId != INVALID_INDEX && shs.Shadow.InstanceIndex != INVALID_INDEX)
				{
					ASSERT(shs.Shadow.BatchId < static_cast<uint32>(m_Batches.size()), "Moved object: shadow batchId OOB.");
					Batch& b = m_Batches[shs.Shadow.BatchId];
					ASSERT(shs.Shadow.InstanceIndex < static_cast<uint32>(b.Instances.size()), "Moved object: shadow instanceIndex OOB.");
					b.Instances[shs.Shadow.InstanceIndex].OwnerObjectDenseIndex = denseIndex;
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

		// Safest: remove -> replace mesh -> reinsert
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
		if (dense == INVALID_INDEX) return nullptr;
		return &m_ObjectDense[(size_t)dense].Obj;
	}

	const RenderScene::SceneObject* RenderScene::GetObjectOrNull(Handle<SceneObject> h) const noexcept
	{
		const uint32 dense = findDenseIndex(h, m_ObjectSlots);
		if (dense == INVALID_INDEX) return nullptr;
		return &m_ObjectDense[(size_t)dense].Obj;
	}

	Handle<RenderScene::IndirectObject> RenderScene::AddIndirect(const IndirectObjectDesc& desc)
	{
		ASSERT(desc.pMesh, "AddIndirect: mesh is null.");
		ASSERT(desc.PassKey != 0, "AddIndirect: PassKey is 0.");

		UniqueHandle<IndirectObject> owner = UniqueHandle<IndirectObject>::Make();
		const Handle<IndirectObject> h = owner.Get();
		ASSERT(h.IsValid(), "Failed to allocate IndirectObject handle.");

		const uint32 handleIndex = h.GetIndex();
		ensureCapacity(handleIndex, m_IndirectSlots);
		ensureCapacity(handleIndex, m_IndirectSparse);

		Slot<IndirectObject>& slot = m_IndirectSlots[handleIndex];
		ASSERT(!slot.bOccupied && !slot.Owner.Get().IsValid(), "IndirectObject slot already occupied.");

		const uint32 denseIndex = static_cast<uint32>(m_IndirectDense.size());

		IndirectObject io = {};
		io.Desc = desc;
		io.bEnabled = true;

		m_IndirectDense.emplace_back(std::move(io));
		m_IndirectHandles.emplace_back(h);

		slot.Owner = std::move(owner);
		slot.DenseIndex = denseIndex;
		slot.bOccupied = true;

		m_IndirectSparse[handleIndex] = denseIndex;
		return h;
	}

	void RenderScene::RemoveIndirect(Handle<IndirectObject> h)
	{
		const uint32 denseIndex = findDenseIndex(h, m_IndirectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to remove non-existing IndirectObject.");

		const uint32 lastIndex = static_cast<uint32>(m_IndirectDense.size() - 1);
		if (denseIndex != lastIndex)
		{
			m_IndirectDense[denseIndex] = std::move(m_IndirectDense[lastIndex]);

			const Handle<IndirectObject> movedHandle = m_IndirectHandles[lastIndex];
			m_IndirectHandles[denseIndex] = movedHandle;

			const uint32 movedHandleIndex = movedHandle.GetIndex();
			Slot<IndirectObject>& movedSlot = m_IndirectSlots[movedHandleIndex];
			ASSERT(movedSlot.bOccupied && movedSlot.Owner.Get() == movedHandle, "Moved slot mismatch.");
			movedSlot.DenseIndex = denseIndex;

			m_IndirectSparse[movedHandleIndex] = denseIndex;
		}

		m_IndirectDense.pop_back();
		m_IndirectHandles.pop_back();

		const uint32 handleIndex = h.GetIndex();
		Slot<IndirectObject>& slot = m_IndirectSlots[handleIndex];
		slot.Owner.Reset();
		slot.DenseIndex = INVALID_INDEX;
		slot.bOccupied = false;
		m_IndirectSparse[handleIndex] = INVALID_INDEX;
	}

	RenderScene::IndirectObject* RenderScene::GetIndirectOrNull(Handle<IndirectObject> h) noexcept
	{
		const uint32 dense = findDenseIndex(h, m_IndirectSlots);
		if (dense == INVALID_INDEX) return nullptr;
		return &m_IndirectDense[(size_t)dense];
	}

	const RenderScene::IndirectObject* RenderScene::GetIndirectOrNull(Handle<IndirectObject> h) const noexcept
	{
		const uint32 dense = findDenseIndex(h, m_IndirectSlots);
		if (dense == INVALID_INDEX) return nullptr;
		return &m_IndirectDense[(size_t)dense];
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
	void RenderScene::BuildDrawPackets(
		uint64 passKey,
		const std::vector<uint32>& visibleObjectDenseIndices,
		const std::function<bool(uint64, MaterialId, IPipelineState**, IShaderResourceBinding**)>& resolver,
		std::vector<DrawPacket>& outPackets,
		std::vector<uint32>& outInstanceRemap) const
	{
		outPackets.clear();
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
			ASSERT(objDense < static_cast<uint32>(m_ObjectDense.size()), "Object dense index OOB.");
			const uint32 oc = m_ObjectDense[objDense].OcIndex;
			ASSERT(oc != INVALID_INDEX && oc < static_cast<uint32>(ocVisible.size()), "Invalid OcIndex.");
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
			if (b.PassKey != passKey)
			{
				continue;
			}

			const uint32 start = static_cast<uint32>(outInstanceRemap.size());
			uint32 count = 0;

			for (const BatchInstance& inst : b.Instances)
			{
				const uint32 oc = inst.OcIndex;
				ASSERT(oc < static_cast<uint32>(ocVisible.size()), "OcIndex OOB.");
				if (ocVisible[oc])
				{
					outInstanceRemap.push_back(oc);
					++count;
				}
			}

			if (count == 0)
			{
				continue;
			}

			// Resolve PSO/SRB
			IPipelineState* pso = nullptr;
			IShaderResourceBinding* srb = nullptr;
			if (!resolver(passKey, b.MaterialId, &pso, &srb))
			{
				// material/pso 준비 안 됐으면 skip (or assert)
				continue;
			}

			// Build DrawPacket
			ASSERT(b.pMesh, "Batch mesh is null.");
			ASSERT(b.SectionIndex < static_cast<uint32>(b.pMesh->Sections.size()), "SectionIndex OOB.");
			const auto& sec = b.pMesh->Sections[b.SectionIndex];

			DrawPacket pkt = {};
			pkt.VertexBuffer = b.pMesh->VertexBuffer;
			pkt.IndexBuffer = b.pMesh->IndexBuffer;
			pkt.PSO = pso;
			pkt.SRB = srb;

			pkt.DrawAttribs = {};
			pkt.DrawAttribs.IndexType = b.pMesh->IndexType;
			pkt.DrawAttribs.NumIndices = sec.IndexCount;
			pkt.DrawAttribs.FirstIndexLocation = sec.FirstIndex;
			pkt.DrawAttribs.BaseVertex = static_cast<int32>(sec.BaseVertex);
			pkt.DrawAttribs.NumInstances = count;
			pkt.DrawAttribs.FirstInstanceLocation = start;
			pkt.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

			outPackets.emplace_back(pkt);
		}
	}

	void RenderScene::BuildIndirectDrawPackets(
		uint64 passKey,
		const std::function<bool(uint64, MaterialId, IPipelineState**, IShaderResourceBinding**)>& resolver,
		std::vector<DrawIndirectPacket>& outPackets) const
	{
		outPackets.clear();

		for (const IndirectObject& io : m_IndirectDense)
		{
			if (!io.bEnabled)
			{
				continue;
			}

			const IndirectObjectDesc& d = io.Desc;

			const bool bIsMainPass = (d.PassKey == passKey);
			const bool bIsShadowPass = (passKey == STRING_HASH("Shadow")) && d.bCastShadow;

			if (!bIsMainPass && !bIsShadowPass)
			{
				continue;
			}

			ASSERT(d.pMesh, "IndirectObject mesh is null.");
			const StaticMeshRenderData* mesh = d.pMesh;

			const uint32 sectionCount = static_cast<uint32>(mesh->Sections.size());
			if (sectionCount == 0)
			{
				continue;
			}

			// 정책: mesh의 section마다 indirect args slot을 하나씩 사용한다.
			// slot = d.IndirectSlot + sectionIndex
			for (uint32 si = 0; si < sectionCount; ++si)
			{
				const StaticMeshRenderData::Section& sec = mesh->Sections[si];

				const uint32 slot = d.IndirectSlot + si;
				// MAX_NUM_INDIRECTS = 256 (HLSL과 맞춰 사용)
				ASSERT(slot < 256u, "IndirectSlot out of range. base=%u, si=%u", d.IndirectSlot, si);

				IPipelineState* pso = nullptr;
				IShaderResourceBinding* srb = nullptr;
				if (!resolver(passKey, sec.MaterialId, &pso, &srb))
				{
					// 파이프라인 준비가 안 됐으면 스킵(혹은 ASSERT로 바꿔도 됨)
					continue;
				}

				DrawIndirectPacket pkt = {};
				pkt.VertexBuffer = mesh->VertexBuffer;
				pkt.IndexBuffer = mesh->IndexBuffer;
				pkt.PSO = pso;
				pkt.SRB = srb;

				// DrawIndexedIndirectAttribs 설정
				// - 실제 인덱스/인스턴스 카운트는 IndirectArgsBuffer(=g_IndirectArgs)에 들어있다.
				// - 여기서는 offset/stride/count/indexType만 채운다.
				// - pAttribsBuffer는 Renderer가 패스 실행 직전에 꽂아준다.
				pkt.DrawAttribs = {};
				pkt.DrawAttribs.IndexType = mesh->IndexType;

				pkt.DrawAttribs.DrawArgsOffset = slot * 20u; // INDIRECT_ARGS_STRIDE_BYTES
				pkt.DrawAttribs.DrawCount = 1;
				pkt.DrawAttribs.DrawArgsStride = 20;

				pkt.DrawAttribs.pAttribsBuffer = nullptr;
				pkt.DrawAttribs.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

				// counter는 안 쓰는 정책
				pkt.DrawAttribs.pCounterBuffer = nullptr;
				pkt.DrawAttribs.CounterOffset = 0;
				pkt.DrawAttribs.CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

				outPackets.emplace_back(pkt);
			}
		}
	}

	bool RenderScene::TryGetBatchView(uint32 batchId, BatchView& outView) const noexcept
	{
		ASSERT(batchId < static_cast<uint32>(m_Batches.size()), "Batch ID out of bounds.");

		const Batch& b = m_Batches[batchId];
		outView.pMesh = b.pMesh;
		outView.SectionIndex = b.SectionIndex;
		outView.MaterialId = b.MaterialId;
		outView.bCastShadow = b.bCastShadow;
		outView.PassKey = b.PassKey;
		return true;
	}

	// ------------------------------------------------------------
	// Terrain
	// ------------------------------------------------------------
	void RenderScene::SetTerrain(RefCntAutoPtr<ITexture> heightMap, const StaticMeshRenderData& terrainMesh, const Matrix4x4& world)
	{
		ClearTerrain();

		m_pTerrainHeightMap = heightMap;
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
	RenderScene::DrawBatchKey RenderScene::makeBatchKey(uint64 passKey, const StaticMeshRenderData& mesh, uint32 sectionIndex, MaterialId matId, bool bCastShadow)
	{
		DrawBatchKey k = {};
		k.MeshPtr = &mesh;
		k.SectionIndex = sectionIndex;
		k.PassKey = passKey;
		k.MatId = matId;
		k.bCastShadow = bCastShadow;
		return k;
	}

	uint32 RenderScene::getOrCreateBatch(const DrawBatchKey& key, const StaticMeshRenderData& mesh, uint32 sectionIndex, MaterialId matId, uint64 passKey, bool bCastShadow)
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
		b.MaterialId = matId;
		b.PassKey = passKey;
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
			BatchInstance moved = batch.Instances[lastIndex];
			batch.Instances[instanceIndex] = moved;

			ASSERT(moved.OwnerObjectDenseIndex < static_cast<uint32>(m_ObjectDense.size()), "batchRemoveInstance: moved owner out of range.");
			ObjectRecord& movedOwner = m_ObjectDense[moved.OwnerObjectDenseIndex];
			ASSERT(moved.OwnerSectionSlot < movedOwner.Sections.size(), "batchRemoveInstance: moved owner section slot out of range.");

			SectionHandles& shs = movedOwner.Sections[moved.OwnerSectionSlot];
			SectionHandle* pHandle = (moved.OwnerPassSlot == 0) ? &shs.Main : &shs.Shadow;

			ASSERT(pHandle->BatchId == batchId, "batchRemoveInstance: moved handle batch mismatch.");
			ASSERT(pHandle->InstanceIndex == lastIndex, "batchRemoveInstance: moved handle instance mismatch.");
			pHandle->InstanceIndex = instanceIndex;
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

		ASSERT(obj.pMesh, "addObjectToBatches: mesh is null.");
		ASSERT(rec.OcIndex != INVALID_INDEX, "Object has no OcIndex.");
		ASSERT(rec.OcIndex < static_cast<uint32>(m_ObjectTableCPU.size()), "OcIndex OOB.");

		// ObjectConstants CPU mirror update (최초 1회)
		m_ObjectTableCPU[rec.OcIndex].World = obj.World;
		m_ObjectTableCPU[rec.OcIndex].WorldInvTranspose = obj.WorldInvTranspose;
		markOcDirty(rec.OcIndex);

		const uint32 sectionCount = static_cast<uint32>(obj.pMesh->Sections.size());

		rec.Sections.clear();
		rec.Sections.resize(sectionCount);

		for (uint32 si = 0; si < sectionCount; ++si)
		{
			const auto& sec = obj.pMesh->Sections[si];

			// NOTE:
			// StaticMeshRenderData::Section은 MaterialId를 가진다고 가정한다.
			// (이 필드명은 네 실제 구조에 맞게 바꿔 끼워라.)
			const MaterialId matId = sec.MaterialId;

			// -----------------------------
			// Main pass batch
			// -----------------------------
			{
				const uint64 mainPassKey = classifyMainPassKey(matId);

				BatchInstance inst = {};
				inst.OcIndex = rec.OcIndex;
				inst.OwnerObjectDenseIndex = objectDenseIndex;
				inst.OwnerSectionSlot = static_cast<uint16>(si);
				inst.OwnerPassSlot = 0;

				const DrawBatchKey key = makeBatchKey(mainPassKey, *obj.pMesh, si, matId, obj.bCastShadow);
				const uint32 batchId = getOrCreateBatch(key, *obj.pMesh, si, matId, mainPassKey, obj.bCastShadow);

				Batch& batch = m_Batches[batchId];
				const uint32 instIndex = static_cast<uint32>(batch.Instances.size());
				batch.Instances.emplace_back(inst);

				rec.Sections[si].Main.BatchId = batchId;
				rec.Sections[si].Main.InstanceIndex = instIndex;
			}

			// -----------------------------
			// Shadow pass batch (optional)
			// -----------------------------
			if (obj.bCastShadow && shouldRenderInShadow(matId))
			{
				BatchInstance inst = {};
				inst.OcIndex = rec.OcIndex;
				inst.OwnerObjectDenseIndex = objectDenseIndex;
				inst.OwnerSectionSlot = static_cast<uint16>(si);
				inst.OwnerPassSlot = 1;

				const DrawBatchKey key = makeBatchKey(STRING_HASH("Shadow"), *obj.pMesh, si, matId, obj.bCastShadow);
				const uint32 batchId = getOrCreateBatch(key, *obj.pMesh, si, matId, STRING_HASH("Shadow"), obj.bCastShadow);

				Batch& batch = m_Batches[batchId];
				const uint32 instIndex = static_cast<uint32>(batch.Instances.size());
				batch.Instances.emplace_back(inst);

				rec.Sections[si].Shadow.BatchId = batchId;
				rec.Sections[si].Shadow.InstanceIndex = instIndex;
			}
		}
	}

	void RenderScene::removeObjectFromBatches(uint32 objectDenseIndex)
	{
		ASSERT(objectDenseIndex < static_cast<uint32>(m_ObjectDense.size()), "removeObjectFromBatches: objectDenseIndex OOB.");

		ObjectRecord& rec = m_ObjectDense[objectDenseIndex];

		for (uint32 si = 0; si < static_cast<uint32>(rec.Sections.size()); ++si)
		{
			SectionHandles& shs = rec.Sections[si];

			if (shs.Main.BatchId != INVALID_INDEX && shs.Main.InstanceIndex != INVALID_INDEX)
			{
				batchRemoveInstance(shs.Main.BatchId, shs.Main.InstanceIndex);
				shs.Main = {};
			}

			if (shs.Shadow.BatchId != INVALID_INDEX && shs.Shadow.InstanceIndex != INVALID_INDEX)
			{
				batchRemoveInstance(shs.Shadow.BatchId, shs.Shadow.InstanceIndex);
				shs.Shadow = {};
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
