#include "pch.h"
#include "RenderScene.h"

#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"
#include "Engine/Renderer/Public/Renderer.h"

namespace shz
{
	// ------------------------------------------------------------
	// LOD helpers
	// ------------------------------------------------------------
	static inline float ComputeScreenSizeFromSphere_ViewSpace(float zPositive, float radius, float tanHalfFovY)
	{
		const float dist = std::max(zPositive, 1e-3f);
		const float ss = (radius / dist) * (1.0f / std::max(tanHalfFovY, 1e-6f));
		return ss;
	}

	static inline uint32 ChooseLODByScreenSize(const StaticMeshRenderData& meshRD, float screenSize)
	{
		if (meshRD.Levels.empty())
			return 0;
		if (meshRD.LODScreenSizes.empty())
			return 0;

		const uint32 levelCount = static_cast<uint32>(meshRD.Levels.size());
		const uint32 threshCount = static_cast<uint32>(meshRD.LODScreenSizes.size());
		const uint32 n = std::min(levelCount, threshCount);

		for (uint32 lod = 0; lod < n; ++lod)
		{
			if (screenSize >= meshRD.LODScreenSizes[lod])
				return lod;
		}

		return levelCount - 1;
	}

	// ------------------------------------------------------------
	// Bounds helpers
	// ------------------------------------------------------------
	Box RenderScene::ComputeWorldAabbFromLocalAabb(const Box& localAabb, const Matrix4x4& world)
	{
		// Transform 8 corners and encapsulate.
		const float3 mn = localAabb.Min();
		const float3 mx = localAabb.Max();

		const float3 corners[8] =
		{
			{ mn.x, mn.y, mn.z },
			{ mx.x, mn.y, mn.z },
			{ mn.x, mx.y, mn.z },
			{ mx.x, mx.y, mn.z },
			{ mn.x, mn.y, mx.z },
			{ mx.x, mn.y, mx.z },
			{ mn.x, mx.y, mx.z },
			{ mx.x, mx.y, mx.z },
		};

		Box out;
		for (uint32 i = 0; i < 8; ++i)
		{
			const float4 p(corners[i].x, corners[i].y, corners[i].z, 1.0f);
			const float4 w = world.MulVector4(p);
			out.Encapsulate(float3(w.x, w.y, w.z));
		}
		return out;
	}

	Sphere RenderScene::ComputeWorldSphereFromLocalSphere(const float3& localCenter, float localRadius, const Matrix4x4& world)
	{
		// Center transform
		const float4 p(localCenter.x, localCenter.y, localCenter.z, 1.0f);
		const float4 w = world.MulVector4(p);
		const float3 c(w.x, w.y, w.z);

		// Radius scale: use max basis vector length
		const float3 axisX = { world._m00, world._m01, world._m02 };
		const float3 axisY = { world._m10, world._m11, world._m12 };
		const float3 axisZ = { world._m20, world._m21, world._m22 };

		const float sx = float3::Length(axisX);
		const float sy = float3::Length(axisY);
		const float sz = float3::Length(axisZ);
		const float s = std::max(sx, std::max(sy, sz));

		return Sphere(c, localRadius * s);
	}

	void RenderScene::updateObjectBounds(uint32 objectDenseIndex)
	{
		ASSERT(objectDenseIndex < static_cast<uint32>(m_ObjectDense.size()), "updateObjectBounds: OOB.");
		ObjectRecord& rec = m_ObjectDense[objectDenseIndex];

		if (!rec.Obj.pMesh || rec.Obj.pMesh->Levels.empty())
		{
			rec.WorldAabb = Box();
			rec.WorldSphere = Sphere();
			return;
		}

		const auto& lod0 = rec.Obj.pMesh->Levels[0];
		const Box localAabb = lod0.LocalBounds.GetBox();
		const float3 localCenter = lod0.LocalBounds.Center;
		const float localRadius = lod0.LocalBounds.Radius;

		rec.WorldAabb = ComputeWorldAabbFromLocalAabb(localAabb, rec.Obj.World);
		rec.WorldSphere = ComputeWorldSphereFromLocalSphere(localCenter, localRadius, rec.Obj.World);
	}

	// ------------------------------------------------------------
	// Visibility scratch
	// ------------------------------------------------------------
	void RenderScene::ensureVisibilityScratchCapacity() const
	{
		const size_t n = m_ObjectTableCPU.size();

		if (m_OcVisibleStamp.size() < n)
			m_OcVisibleStamp.resize(n, 0);

		if (m_OcChosenLod.size() < n)
			m_OcChosenLod.resize(n, 0);

		if (m_VisStampCounter == 0)
		{
			std::fill(m_OcVisibleStamp.begin(), m_OcVisibleStamp.end(), 0u);
			m_VisStampCounter = 1;
		}
	}

	uint64 RenderScene::computeVisCacheKey(const ViewLodParams& view, const ViewFrustumExt& frustum) const
	{
		// Cheap key: hash a few matrix elements + tanFov + first plane values.
		// Enough to reuse within a frame for same view/frustum calls.
		const uint64 a = static_cast<uint64>(hash::twang_mix64(*reinterpret_cast<const uint64*>(&view.TanHalfFovY)));
		const uint64 b = static_cast<uint64>(hash::twang_mix64(*reinterpret_cast<const uint64*>(&view.View._m00)));
		const uint64 c = static_cast<uint64>(hash::twang_mix64(*reinterpret_cast<const uint64*>(&view.View._m22)));
		const uint64 d = static_cast<uint64>(hash::twang_mix64(*reinterpret_cast<const uint64*>(&frustum.LeftPlane.Distance)));
		return a ^ (b << 1) ^ (c << 7) ^ (d << 13);
	}

	// ------------------------------------------------------------
	// Spatial structures
	// ------------------------------------------------------------
	void RenderScene::ConfigureDynamicGrid(const LooseGrid::CreateInfo& ci)
	{
		m_DynamicGrid.Initialize(ci);
	}

	void RenderScene::rebuildStaticBVHIfNeeded() const
	{
		if (!m_StaticBVHDirty)
			return;

		// Gather static objects
		std::vector<BVHEntry> entries;
		entries.reserve(m_ObjectDense.size());

		for (uint32 i = 0; i < static_cast<uint32>(m_ObjectDense.size()); ++i)
		{
			const ObjectRecord& rec = m_ObjectDense[i];
			if (!rec.Obj.pMesh)
				continue;

			if (rec.Obj.ObjMobility != SceneObject::Mobility::Static)
				continue;

			BVHEntry e = {};
			e.WorldAabb = rec.WorldAabb;
			e.WorldSphere = rec.WorldSphere;
			e.Payload = i; // object dense index
			entries.emplace_back(e);
		}

		m_StaticBVH.Build(entries);
		m_StaticBVHDirty = false;
	}

	void RenderScene::updateSpatialForObject(uint32 objectDenseIndex)
	{
		ObjectRecord& rec = m_ObjectDense[objectDenseIndex];

		// Always update bounds first
		updateObjectBounds(objectDenseIndex);

		if (rec.Obj.ObjMobility == SceneObject::Mobility::Static)
		{
			// Ensure it isn't in dynamic grid
			m_DynamicGrid.Remove(objectDenseIndex);

			// Mark BVH dirty (rebuild lazily)
			m_StaticBVHDirty = true;
		}
		else
		{
			// Dynamic: maintain in grid
			m_DynamicGrid.InsertOrUpdate(objectDenseIndex, rec.WorldAabb);
		}
	}

	void RenderScene::buildVisibilityAndLodCached(const ViewLodParams& view, const ViewFrustumExt& frustum) const
	{
		ensureVisibilityScratchCapacity();

		const uint64 key = computeVisCacheKey(view, frustum);
		if (key == m_LastVisKey && m_LastVisStamp != 0)
		{
			// already computed for this view/frustum in this frame scope
			return;
		}

		// New visibility stamp
		const uint32 stamp = m_VisStampCounter++;
		m_LastVisKey = key;
		m_LastVisStamp = stamp;

		// Ensure BVH is up-to-date
		rebuildStaticBVHIfNeeded();

		// Query static BVH -> candidate object dense indices
		m_StaticBVHCandidates = m_StaticBVH.QueryFrustum(frustum);

		// Query dynamic grid -> candidate object dense indices
		m_DynamicCandidates = m_DynamicGrid.QueryFrustum(frustum);

		// Helper lambda: process a candidate set
		auto ProcessCandidates = [&](const std::vector<uint32>& candidates)
			{
				for (uint32 objDense : candidates)
				{
					if (objDense >= static_cast<uint32>(m_ObjectDense.size()))
						continue;

					const ObjectRecord& rec = m_ObjectDense[objDense];
					const SceneObject& obj = rec.Obj;

					if (!obj.pMesh)
						continue;
					if (rec.OcIndex == INVALID_INDEX)
						continue;

					const uint32 oc = rec.OcIndex;
					ASSERT(oc < static_cast<uint32>(m_ObjectTableCPU.size()), "OcIndex OOB.");

					// Final frustum test (tight): use OBB test via localAABB + world,
					// BUT since we cached world bounds, do world-AABB plane test quickly:
					// (If you want exact OBB test, keep IntersectsFrustum with localAABB.)
					// We'll use worldAABB plane test via StaticBVH's same method pattern is already used.
					// Simpler: reuse IntersectsFrustum with localAABB to keep correctness. (still cheap because candidates reduced)
					const Box localBounds = obj.pMesh->Levels[0].LocalBounds.GetBox();
					if (!IntersectsFrustum(frustum, localBounds, obj.World, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
						continue;

					m_OcVisibleStamp[oc] = stamp;

					// LOD selection (screen-size)
					const float3 localC = obj.pMesh->Levels[0].LocalBounds.Center;
					const float  radius = obj.pMesh->Levels[0].LocalBounds.Radius;

					const float4 cVS4 = float4(localC, 1.0f) * obj.World * view.View;
					float z = cVS4.z;
					if (!view.bViewForwardIsPositiveZ)
						z = -z;

					const float screenSize = ComputeScreenSizeFromSphere_ViewSpace(std::max(z, 1e-3f), radius, view.TanHalfFovY);
					const uint32 lod = ChooseLODByScreenSize(*obj.pMesh, screenSize);

					m_OcChosenLod[oc] = static_cast<uint16>(lod);
				}
			};

		ProcessCandidates(m_StaticBVHCandidates);
		ProcessCandidates(m_DynamicCandidates);
	}

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
		for (Slot<TerrainObject>& s : m_TerrainSlots)
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
		m_TerrainSparse.clear();
		m_IndirectSparse.clear();
		m_LightSparse.clear();

		m_ObjectDense.clear();
		m_ObjectHandles.clear();

		m_TerrainDense.clear();
		m_TerrainHandles.clear();

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

		m_InteractionStamps.clear();

		m_OcVisibleStamp.clear();
		m_OcChosenLod.clear();
		m_VisStampCounter = 1;

		m_LastVisKey = 0;
		m_LastVisStamp = 0;

		m_StaticBVHDirty = true;
		m_StaticBVH.Reset();
		m_StaticBVHCandidates.clear();

		m_DynamicGrid.Reset();
		m_DynamicCandidates.clear();

		// default grid
		ConfigureDynamicGrid(LooseGrid::CreateInfo{});
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
		rec.Obj.bDepthPrepass = true;
		rec.Obj.ObjMobility = SceneObject::Mobility::Static;

		rec.OcIndex = allocOcIndex();

		m_ObjectDense.emplace_back(std::move(rec));
		m_ObjectHandles.emplace_back(h);

		slot.Owner = std::move(owner);
		slot.DenseIndex = denseIndex;
		slot.bOccupied = true;

		m_ObjectSparse[handleIndex] = denseIndex;

		// Batches + bounds + spatial
		addObjectToBatches(denseIndex);
		updateSpatialForObject(denseIndex);

		return h;
	}

	void RenderScene::UpdateObjectMobility(Handle<SceneObject> h, SceneObject::Mobility mobility)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to set mobility on non-existing SceneObject.");

		ObjectRecord& rec = m_ObjectDense[denseIndex];
		if (rec.Obj.ObjMobility == mobility)
			return;

		rec.Obj.ObjMobility = mobility;

		// Move between spatial structures
		updateSpatialForObject(denseIndex);

		// Visibility cache invalid
		m_LastVisKey = 0;
		m_LastVisStamp = 0;
	}

	void RenderScene::RemoveObject(Handle<SceneObject> h)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to remove non-existing SceneObject.");

		ASSERT(denseIndex < static_cast<uint32>(m_ObjectDense.size()), "Dense index out of range.");
		ASSERT(m_ObjectHandles[denseIndex] == h, "Dense handle mismatch (internal corruption).");

		{
			ObjectRecord& rec = m_ObjectDense[denseIndex];

			// Spatial remove
			m_DynamicGrid.Remove(denseIndex);
			if (rec.Obj.ObjMobility == SceneObject::Mobility::Static)
				m_StaticBVHDirty = true;

			removeObjectFromBatches(denseIndex);
			freeOcIndex(rec.OcIndex);
			rec.OcIndex = INVALID_INDEX;
		}

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

			// Fix backrefs for moved object's batch instances
			ObjectRecord& movedRec = m_ObjectDense[denseIndex];
			for (uint32 lod = 0; lod < static_cast<uint32>(movedRec.SectionsByLod.size()); ++lod)
			{
				auto& secVec = movedRec.SectionsByLod[lod];
				for (uint32 si = 0; si < static_cast<uint32>(secVec.size()); ++si)
				{
					const SectionHandles& shs = secVec[si];

					auto Fix = [&](const SectionHandle& hnd)
						{
							if (hnd.BatchId != INVALID_INDEX && hnd.InstanceIndex != INVALID_INDEX)
							{
								ASSERT(hnd.BatchId < static_cast<uint32>(m_Batches.size()), "Moved object: batchId OOB.");
								Batch& b = m_Batches[hnd.BatchId];
								ASSERT(hnd.InstanceIndex < static_cast<uint32>(b.Instances.size()), "Moved object: instanceIndex OOB.");
								b.Instances[hnd.InstanceIndex].OwnerObjectDenseIndex = denseIndex;
							}
						};

					Fix(shs.Main);
					Fix(shs.Shadow);
					Fix(shs.Depth);
				}
			}

			// Spatial structures: payload id is "dense index", so we must update grid mapping:
			// Remove old payload (lastIndex) and reinsert as new payload (denseIndex) for dynamic objects.
			// Static BVH rebuild anyway.
			if (movedRec.Obj.ObjMobility == SceneObject::Mobility::Dynamic)
			{
				m_DynamicGrid.Remove(lastIndex);
				m_DynamicGrid.InsertOrUpdate(denseIndex, movedRec.WorldAabb);
			}
			else
			{
				m_StaticBVHDirty = true;
			}
		}

		m_ObjectDense.pop_back();
		m_ObjectHandles.pop_back();

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

		m_LastVisKey = 0;
		m_LastVisStamp = 0;
	}

	void RenderScene::UpdateObjectMesh(Handle<SceneObject> h, const StaticMeshRenderData& mesh)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to update non-existing SceneObject.");

		ObjectRecord& rec = m_ObjectDense[denseIndex];

		removeObjectFromBatches(denseIndex);
		rec.Obj.pMesh = &mesh;
		addObjectToBatches(denseIndex);

		// bounds & spatial
		updateSpatialForObject(denseIndex);

		m_LastVisKey = 0;
		m_LastVisStamp = 0;
	}

	void RenderScene::UpdateObjectTransform(Handle<SceneObject> h, const Matrix4x4& world)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to update non-existing SceneObject.");

		ObjectRecord& rec = m_ObjectDense[denseIndex];
		rec.Obj.World = world;
		rec.Obj.WorldInvTranspose = world.Inversed().Transposed();

		ASSERT(rec.OcIndex != INVALID_INDEX, "Object has no OcIndex.");
		m_ObjectTableCPU[rec.OcIndex].PrevWorld = m_ObjectTableCPU[rec.OcIndex].World;
		m_ObjectTableCPU[rec.OcIndex].World = rec.Obj.World;
		m_ObjectTableCPU[rec.OcIndex].WorldInvTranspose = rec.Obj.WorldInvTranspose;
		markOcDirty(rec.OcIndex);

		// bounds & spatial
		updateSpatialForObject(denseIndex);

		m_LastVisKey = 0;
		m_LastVisStamp = 0;
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

	// ------------------------------------------------------------
	// Terrain: Add/Remove/Get
	// ------------------------------------------------------------
	Handle<RenderScene::TerrainObject> RenderScene::AddTerrain(const TerrainObject& obj)
	{
		ASSERT(obj.VertexBuffer, "AddTerrain: VertexBuffer is null.");
		ASSERT(obj.IndexBuffer, "AddTerrain: IndexBuffer is null.");
		ASSERT(obj.IndexCount > 0, "AddTerrain: IndexCount is 0.");
		ASSERT(obj.InstanceCount > 0, "AddTerrain: InstanceCount is 0.");
		ASSERT(obj.MaterialId != 0, "AddTerrain: MaterialId is 0.");

		UniqueHandle<TerrainObject> owner = UniqueHandle<TerrainObject>::Make();
		const Handle<TerrainObject> h = owner.Get();
		ASSERT(h.IsValid(), "Failed to allocate TerrainObject handle.");

		const uint32 handleIndex = h.GetIndex();
		ensureCapacity(handleIndex, m_TerrainSlots);
		ensureCapacity(handleIndex, m_TerrainSparse);

		Slot<TerrainObject>& slot = m_TerrainSlots[handleIndex];
		ASSERT(!slot.bOccupied && !slot.Owner.Get().IsValid(), "TerrainObject slot already occupied.");

		const uint32 denseIndex = static_cast<uint32>(m_TerrainDense.size());

		m_TerrainDense.emplace_back(obj);
		m_TerrainHandles.emplace_back(h);

		slot.Owner = std::move(owner);
		slot.DenseIndex = denseIndex;
		slot.bOccupied = true;

		m_TerrainSparse[handleIndex] = denseIndex;
		return h;
	}

	void RenderScene::RemoveTerrain(Handle<TerrainObject> h)
	{
		const uint32 denseIndex = findDenseIndex(h, m_TerrainSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to remove non-existing TerrainObject.");

		const uint32 lastIndex = static_cast<uint32>(m_TerrainDense.size() - 1);
		if (denseIndex != lastIndex)
		{
			m_TerrainDense[denseIndex] = std::move(m_TerrainDense[lastIndex]);

			const Handle<TerrainObject> movedHandle = m_TerrainHandles[lastIndex];
			m_TerrainHandles[denseIndex] = movedHandle;

			const uint32 movedHandleIndex = movedHandle.GetIndex();
			Slot<TerrainObject>& movedSlot = m_TerrainSlots[movedHandleIndex];
			ASSERT(movedSlot.bOccupied && movedSlot.Owner.Get() == movedHandle, "Moved slot mismatch.");
			movedSlot.DenseIndex = denseIndex;

			m_TerrainSparse[movedHandleIndex] = denseIndex;
		}

		m_TerrainDense.pop_back();
		m_TerrainHandles.pop_back();

		const uint32 handleIndex = h.GetIndex();
		Slot<TerrainObject>& slot = m_TerrainSlots[handleIndex];
		slot.Owner.Reset();
		slot.DenseIndex = INVALID_INDEX;
		slot.bOccupied = false;

		m_TerrainSparse[handleIndex] = INVALID_INDEX;
	}

	RenderScene::TerrainObject* RenderScene::GetTerrainOrNull(Handle<TerrainObject> h) noexcept
	{
		const uint32 dense = findDenseIndex(h, m_TerrainSlots);
		if (dense == INVALID_INDEX) return nullptr;
		return &m_TerrainDense[(size_t)dense];
	}

	const RenderScene::TerrainObject* RenderScene::GetTerrainOrNull(Handle<TerrainObject> h) const noexcept
	{
		const uint32 dense = findDenseIndex(h, m_TerrainSlots);
		if (dense == INVALID_INDEX) return nullptr;
		return &m_TerrainDense[(size_t)dense];
	}

	// ------------------------------------------------------------
	// Indirect: Add/Remove/Get
	// ------------------------------------------------------------
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
	// Indirect packets
	// ------------------------------------------------------------
	void RenderScene::BuildIndirectDrawPackets(
		uint64 passKey,
		const std::function<const MaterialPipelineBinding& (MaterialId, uint64)>& resolver,
		std::vector<DrawIndirectPacket>& outPackets) const
	{
		outPackets.clear();

		for (const IndirectObject& io : m_IndirectDense)
		{
			if (!io.bEnabled)
				continue;

			const IndirectObjectDesc& d = io.Desc;

			const bool bIsMainPass = (d.PassKey == passKey);
			const bool bIsShadowPass = (passKey == STRING_HASH("Shadow")) && d.bCastShadow;
			const bool bIsDepthPrepass = (passKey == STRING_HASH("DepthPrepass")) && d.bDepthPrepass;

			if (!bIsMainPass && !bIsShadowPass && !bIsDepthPrepass)
				continue;

			ASSERT(d.pMesh, "IndirectObject mesh is null.");
			const StaticMeshLevelRenderData* mesh = d.pMesh;

			const uint32 sectionCount = static_cast<uint32>(mesh->Sections.size());
			ASSERT(sectionCount > 0, "IndirectObject mesh has no sections.");

			for (uint32 si = 0; si < sectionCount; ++si)
			{
				const StaticMeshLevelRenderData::Section& sec = mesh->Sections[si];

				const uint32 slot = d.IndirectBaseSlot + si;
				ASSERT(slot < MAX_NUM_INDIRECTS, "Indirect slot out of range. base=%u si=%u", d.IndirectBaseSlot, si);

				const MaterialPipelineBinding& pb = resolver(sec.MaterialId, passKey);
				ASSERT(pb.pPSO && pb.pSRB, "Pipeline binding is null.");

				DrawIndirectPacket pkt = {};
				pkt.VertexBuffer = mesh->VertexBuffer;
				pkt.IndexBuffer = mesh->IndexBuffer;
				pkt.PSO = pb.pPSO;
				pkt.SRB = pb.pSRB;

				pkt.DrawAttribs = {};
				pkt.DrawAttribs.IndexType = mesh->IndexType;

				pkt.DrawAttribs.DrawArgsOffset = static_cast<uint64>(slot) * 20u;
				pkt.DrawAttribs.DrawCount = 1;
				pkt.DrawAttribs.DrawArgsStride = 20u;

				pkt.DrawAttribs.pAttribsBuffer = nullptr;
				pkt.DrawAttribs.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

				pkt.DrawAttribs.pCounterBuffer = nullptr;
				pkt.DrawAttribs.CounterOffset = static_cast<uint64>(slot) * 4u;
				pkt.DrawAttribs.CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;

				pkt.StartInstanceLocation = io.Desc.StartInstanceLocation;

				outPackets.emplace_back(pkt);
			}
		}
	}

	// ------------------------------------------------------------
	// Terrain packets
	// ------------------------------------------------------------
	void RenderScene::BuildTerrainDrawPackets(
		uint64 passKey,
		const std::function<const MaterialPipelineBinding& (MaterialId, uint64)>& resolver,
		std::vector<DrawPacket>& outPackets) const
	{
		if (!(passKey == STRING_HASH("GBuffer") || passKey == STRING_HASH("Shadow") || passKey == STRING_HASH("DepthPrepass")))
			return;

		for (const TerrainObject& t : m_TerrainDense)
		{
			ASSERT(t.VertexBuffer, "TerrainObject VB is null.");
			ASSERT(t.IndexBuffer, "TerrainObject IB is null.");
			ASSERT(t.IndexCount > 0, "TerrainObject IndexCount is 0.");

			const MaterialPipelineBinding& pb = resolver(t.MaterialId, passKey);
			ASSERT(pb.pPSO && pb.pSRB, "Terrain pipeline binding is null.");

			DrawPacket pkt = {};
			pkt.VertexBuffer = t.VertexBuffer;
			pkt.IndexBuffer = t.IndexBuffer;
			pkt.PSO = pb.pPSO;
			pkt.SRB = pb.pSRB;

			pkt.StartInstanceLocation = t.StartInstanceLocation;

			pkt.DrawAttribs = {};
			pkt.DrawAttribs.IndexType = t.IndexType;
			pkt.DrawAttribs.NumIndices = t.IndexCount;
			pkt.DrawAttribs.FirstIndexLocation = 0;
			pkt.DrawAttribs.BaseVertex = 0;
			pkt.DrawAttribs.NumInstances = t.InstanceCount;
			pkt.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

			outPackets.emplace_back(pkt);
		}
	}

	// ------------------------------------------------------------
	// Draw list build (BVH+Grid visibility + LOD + batching)
	// ------------------------------------------------------------
	void RenderScene::BuildDrawPackets(
		uint64 passKey,
		const ViewLodParams& view,
		const ViewFrustumExt& frustum,
		const std::function<const MaterialPipelineBinding& (MaterialId, uint64)>& resolver,
		std::vector<DrawPacket>& outPackets,
		std::vector<uint32>& outInstanceRemap) const
	{
		outPackets.clear();
		outInstanceRemap.clear();

		// Terrain first
		BuildTerrainDrawPackets(passKey, resolver, outPackets);

		if (m_ObjectDense.empty() || m_ObjectTableCPU.empty())
		{
			return;
		}

		// Visibility + LOD (cached)
		buildVisibilityAndLodCached(view, frustum);

		const uint32 stamp = m_LastVisStamp;
		const bool bIsShadowPass = (passKey == STRING_HASH("Shadow"));

		// Iterate batches (already LOD-specific)
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

			if (bIsShadowPass && !b.bMatCastsShadow)
			{
				continue;
			}

			const uint32 start = static_cast<uint32>(outInstanceRemap.size());

			// Single pass push
			for (const BatchInstance& inst : b.Instances)
			{
				const uint32 oc = inst.OcIndex;
				if (oc >= static_cast<uint32>(m_OcVisibleStamp.size()))
				{
					continue;
				}

				if (m_OcVisibleStamp[oc] != stamp)
				{
					continue;
				}

				if (m_OcChosenLod[oc] != b.LodIndex)
				{
					continue;
				}

				outInstanceRemap.push_back(oc);
			}

			const uint32 visibleCount = static_cast<uint32>(outInstanceRemap.size()) - start;
			if (visibleCount == 0)
			{
				// rollback
				outInstanceRemap.resize(start);
				continue;
			}

			ASSERT(b.pMesh, "Batch mesh is null.");

			const StaticMeshRenderData& meshRD = *b.pMesh;
			ASSERT(b.LodIndex < static_cast<uint32>(meshRD.Levels.size()), "Batch LodIndex OOB.");
			const StaticMeshLevelRenderData& lvl = meshRD.Levels[b.LodIndex];

			ASSERT(b.SectionIndex < static_cast<uint32>(lvl.Sections.size()), "Batch SectionIndex OOB for this LOD.");
			const auto& sec = lvl.Sections[b.SectionIndex];

			const MaterialPipelineBinding& pb = resolver(b.MaterialId, passKey);
			ASSERT(pb.pPSO && pb.pSRB, "Pipeline binding is null.");

			DrawPacket pkt = {};
			pkt.VertexBuffer = lvl.VertexBuffer;
			pkt.IndexBuffer = lvl.IndexBuffer;
			pkt.PSO = pb.pPSO;
			pkt.SRB = pb.pSRB;

			pkt.StartInstanceLocation = start;

			pkt.DrawAttribs = {};
			pkt.DrawAttribs.IndexType = lvl.IndexType;
			pkt.DrawAttribs.NumIndices = sec.IndexCount;
			pkt.DrawAttribs.FirstIndexLocation = sec.FirstIndex;
			pkt.DrawAttribs.BaseVertex = static_cast<int32>(sec.BaseVertex);
			pkt.DrawAttribs.NumInstances = visibleCount;
			pkt.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

			outPackets.emplace_back(pkt);
		}
	}

	bool RenderScene::TryGetBatchView(uint32 batchId, BatchView& outView) const noexcept
	{
		ASSERT(batchId < static_cast<uint32>(m_Batches.size()), "Batch ID out of bounds.");

		const Batch& b = m_Batches[batchId];
		outView.pMesh = b.pMesh;
		outView.LodIndex = b.LodIndex;
		outView.SectionIndex = b.SectionIndex;
		outView.MaterialId = b.MaterialId;
		outView.bCastShadow = b.bCastShadow;
		outView.PassKey = b.PassKey;
		return true;
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
			return;

		ASSERT(ocIndex < static_cast<uint32>(m_ObjectTableCPU.size()), "freeOcIndex out of range.");
		m_FreeOcIndices.push_back(ocIndex);

		if (ocIndex < static_cast<uint32>(m_OcDirty.size()))
			m_OcDirty[ocIndex] = 0;
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
	RenderScene::DrawBatchKey RenderScene::makeBatchKey(
		uint64 passKey,
		const StaticMeshRenderData& mesh,
		uint32 lodIndex,
		uint32 sectionIndex,
		MaterialId matId,
		bool bCastShadow)
	{
		DrawBatchKey k = {};
		k.MeshPtr = &mesh;
		k.LodIndex = lodIndex;
		k.SectionIndex = sectionIndex;
		k.PassKey = passKey;
		k.MatId = matId;
		k.bCastShadow = bCastShadow;
		return k;
	}

	uint32 RenderScene::getOrCreateBatch(
		const DrawBatchKey& key,
		const StaticMeshRenderData& mesh,
		uint32 lodIndex,
		uint32 sectionIndex,
		MaterialId matId,
		uint64 passKey,
		bool bCastShadow)
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
		b.LodIndex = lodIndex;
		b.SectionIndex = sectionIndex;
		b.MaterialId = matId;
		b.PassKey = passKey;
		b.bCastShadow = bCastShadow;
		b.bMatCastsShadow = shouldRenderInShadow(matId);

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

			ASSERT(moved.OwnerLodIndex < movedOwner.SectionsByLod.size(), "batchRemoveInstance: moved owner lod OOB.");
			auto& lodSecs = movedOwner.SectionsByLod[moved.OwnerLodIndex];

			ASSERT(moved.OwnerSectionIndex < lodSecs.size(), "batchRemoveInstance: moved owner section OOB.");
			SectionHandles& shs = lodSecs[moved.OwnerSectionIndex];

			SectionHandle* pHandle = nullptr;
			if (moved.OwnerPassSlot == 0) pHandle = &shs.Main;
			else if (moved.OwnerPassSlot == 1) pHandle = &shs.Shadow;
			else pHandle = &shs.Depth;

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

		// OC table
		m_ObjectTableCPU[rec.OcIndex].World = obj.World;
		m_ObjectTableCPU[rec.OcIndex].WorldInvTranspose = obj.WorldInvTranspose;
		m_ObjectTableCPU[rec.OcIndex].PrevWorld = Matrix4x4::Identity();
		markOcDirty(rec.OcIndex);

		const StaticMeshRenderData& meshRD = *obj.pMesh;
		ASSERT(!meshRD.Levels.empty(), "StaticMeshRenderData has no levels.");

		const uint32 lodCount = static_cast<uint32>(meshRD.Levels.size());

		rec.SectionsByLod.clear();
		rec.SectionsByLod.resize(lodCount);

		for (uint32 lod = 0; lod < lodCount; ++lod)
		{
			const StaticMeshLevelRenderData& lvl = meshRD.Levels[lod];
			const uint32 sectionCount = static_cast<uint32>(lvl.Sections.size());

			rec.SectionsByLod[lod].clear();
			rec.SectionsByLod[lod].resize(sectionCount);

			for (uint32 si = 0; si < sectionCount; ++si)
			{
				const auto& sec = lvl.Sections[si];
				const MaterialId matId = sec.MaterialId;

				// Main pass
				{
					const uint64 mainPassKey = classifyMainPassKey(matId);

					BatchInstance inst = {};
					inst.OcIndex = rec.OcIndex;
					inst.OwnerObjectDenseIndex = objectDenseIndex;
					inst.OwnerLodIndex = static_cast<uint16>(lod);
					inst.OwnerSectionIndex = static_cast<uint16>(si);
					inst.OwnerPassSlot = 0;

					const DrawBatchKey key = makeBatchKey(mainPassKey, meshRD, lod, si, matId, obj.bCastShadow);
					const uint32 batchId = getOrCreateBatch(key, meshRD, lod, si, matId, mainPassKey, obj.bCastShadow);

					Batch& batch = m_Batches[batchId];
					const uint32 instIndex = static_cast<uint32>(batch.Instances.size());
					batch.Instances.emplace_back(inst);

					rec.SectionsByLod[lod][si].Main.BatchId = batchId;
					rec.SectionsByLod[lod][si].Main.InstanceIndex = instIndex;
				}

				// Shadow
				if (obj.bCastShadow && shouldRenderInShadow(matId))
				{
					BatchInstance inst = {};
					inst.OcIndex = rec.OcIndex;
					inst.OwnerObjectDenseIndex = objectDenseIndex;
					inst.OwnerLodIndex = static_cast<uint16>(lod);
					inst.OwnerSectionIndex = static_cast<uint16>(si);
					inst.OwnerPassSlot = 1;

					const DrawBatchKey key = makeBatchKey(STRING_HASH("Shadow"), meshRD, lod, si, matId, obj.bCastShadow);
					const uint32 batchId = getOrCreateBatch(key, meshRD, lod, si, matId, STRING_HASH("Shadow"), obj.bCastShadow);

					Batch& batch = m_Batches[batchId];
					const uint32 instIndex = static_cast<uint32>(batch.Instances.size());
					batch.Instances.emplace_back(inst);

					rec.SectionsByLod[lod][si].Shadow.BatchId = batchId;
					rec.SectionsByLod[lod][si].Shadow.InstanceIndex = instIndex;
				}

				// Depth prepass
				if (obj.bDepthPrepass)
				{
					BatchInstance inst = {};
					inst.OcIndex = rec.OcIndex;
					inst.OwnerObjectDenseIndex = objectDenseIndex;
					inst.OwnerLodIndex = static_cast<uint16>(lod);
					inst.OwnerSectionIndex = static_cast<uint16>(si);
					inst.OwnerPassSlot = 2;

					const DrawBatchKey key = makeBatchKey(STRING_HASH("DepthPrepass"), meshRD, lod, si, matId, obj.bCastShadow);
					const uint32 batchId = getOrCreateBatch(key, meshRD, lod, si, matId, STRING_HASH("DepthPrepass"), obj.bCastShadow);

					Batch& batch = m_Batches[batchId];
					const uint32 instIndex = static_cast<uint32>(batch.Instances.size());
					batch.Instances.emplace_back(inst);

					rec.SectionsByLod[lod][si].Depth.BatchId = batchId;
					rec.SectionsByLod[lod][si].Depth.InstanceIndex = instIndex;
				}
			}
		}
	}

	void RenderScene::removeObjectFromBatches(uint32 objectDenseIndex)
	{
		ASSERT(objectDenseIndex < static_cast<uint32>(m_ObjectDense.size()), "removeObjectFromBatches: objectDenseIndex OOB.");

		ObjectRecord& rec = m_ObjectDense[objectDenseIndex];

		for (uint32 lod = 0; lod < static_cast<uint32>(rec.SectionsByLod.size()); ++lod)
		{
			auto& secVec = rec.SectionsByLod[lod];

			for (uint32 si = 0; si < static_cast<uint32>(secVec.size()); ++si)
			{
				SectionHandles& shs = secVec[si];

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

				if (shs.Depth.BatchId != INVALID_INDEX && shs.Depth.InstanceIndex != INVALID_INDEX)
				{
					batchRemoveInstance(shs.Depth.BatchId, shs.Depth.InstanceIndex);
					shs.Depth = {};
				}
			}
		}

		rec.SectionsByLod.clear();
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
			return INVALID_INDEX;

		if (slot.Owner.Get() != h)
			return INVALID_INDEX;

		return slot.DenseIndex;
	}

} // namespace shz