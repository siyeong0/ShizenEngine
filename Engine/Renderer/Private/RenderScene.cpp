// RenderScene.cpp
#include "pch.h"
#include "RenderScene.h"

namespace shz
{
	void RenderScene::Reset()
	{
		for (Slot<RenderObject>& s : m_ObjectSlots)
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

		m_Objects.clear();
		m_ObjectHandles.clear();

		m_Lights.clear();
		m_LightHandles.clear();
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

	// ------------------------------------------------------------
	// RenderObjects
	// ------------------------------------------------------------

	Handle<RenderScene::RenderObject> RenderScene::AddObject(Handle<StaticMeshRenderData> meshHandle, const Matrix4x4& transform)
	{
		UniqueHandle<RenderObject> owner = UniqueHandle<RenderObject>::Make();
		const Handle<RenderObject> h = owner.Get();
		ASSERT(h.IsValid(), "Failed to allocate RenderObject handle.");

		const uint32 handleIndex = h.GetIndex();
		ensureCapacity(handleIndex, m_ObjectSlots);
		ensureCapacity(handleIndex, m_ObjectSparse);

		Slot<RenderObject>& slot = m_ObjectSlots[handleIndex];
		ASSERT(!slot.bOccupied && !slot.Owner.Get().IsValid(), "RenderObject slot already occupied.");

		const uint32 denseIndex = static_cast<uint32>(m_Objects.size());

		RenderObject obj = {};
		obj.MeshHandle = meshHandle;
		obj.Transform = transform;

		// Store dense
		m_Objects.push_back(std::move(obj));
		m_ObjectHandles.push_back(h);

		// Bind sparse/slot
		slot.Owner = std::move(owner);
		slot.DenseIndex = denseIndex;
		slot.bOccupied = true;

		m_ObjectSparse[handleIndex] = denseIndex;

		return h;
	}

	void RenderScene::RemoveObject(Handle<RenderObject> h)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to remove non-existing RenderObject.");

		ASSERT(denseIndex < static_cast<uint32>(m_Objects.size()), "Dense index out of range.");
		ASSERT(m_ObjectHandles[denseIndex] == h, "Dense handle mismatch (internal corruption).");

		const uint32 lastIndex = static_cast<uint32>(m_Objects.size() - 1);
		if (denseIndex != lastIndex)
		{
			// Move last -> removed spot
			m_Objects[denseIndex] = std::move(m_Objects[lastIndex]);

			const Handle<RenderObject> movedHandle = m_ObjectHandles[lastIndex];
			m_ObjectHandles[denseIndex] = movedHandle;

			// Fix moved handle's slot
			const uint32 movedHandleIndex = movedHandle.GetIndex();
			ASSERT(movedHandleIndex < static_cast<uint32>(m_ObjectSlots.size()), "Moved handle slot missing.");
			auto& movedSlot = m_ObjectSlots[movedHandleIndex];
			ASSERT(movedSlot.bOccupied && movedSlot.Owner.Get() == movedHandle, "Moved slot mismatch.");
			movedSlot.DenseIndex = denseIndex;

			// Fix sparse mapping
			ASSERT(movedHandleIndex < static_cast<uint32>(m_ObjectSparse.size()), "Moved sparse missing.");
			m_ObjectSparse[movedHandleIndex] = denseIndex;
		}

		// Pop dense
		m_Objects.pop_back();
		m_ObjectHandles.pop_back();

		// Clear slot/sparse for removed
		const uint32 handleIndex = h.GetIndex();
		ASSERT(handleIndex < static_cast<uint32>(m_ObjectSlots.size()), "Handle slot missing.");
		auto& slot = m_ObjectSlots[handleIndex];
		slot.Owner.Reset();
		slot.DenseIndex = INVALID_INDEX;
		slot.bOccupied = false;

		if (handleIndex < static_cast<uint32>(m_ObjectSparse.size()))
		{
			m_ObjectSparse[handleIndex] = INVALID_INDEX;
		}
	}

	void RenderScene::UpdateObjectTransform(Handle<RenderObject> h, const Matrix4x4& world)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to update non-existing RenderObject.");

		m_Objects[denseIndex].Transform = world;
	}

	void RenderScene::UpdateObjectMesh(Handle<RenderObject> h, Handle<StaticMeshRenderData> mesh)
	{
		const uint32 denseIndex = findDenseIndex(h, m_ObjectSlots);
		ASSERT(denseIndex != INVALID_INDEX, "Attempted to update non-existing RenderObject.");

		m_Objects[denseIndex].MeshHandle = mesh;
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

		const uint32 denseIndex = static_cast<uint32>(m_Lights.size());

		// Store dense
		m_Lights.push_back(light);
		m_LightHandles.push_back(h);

		// Bind sparse/slot
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

		ASSERT(denseIndex < static_cast<uint32>(m_Lights.size()), "Dense index out of range.");
		ASSERT(m_LightHandles[denseIndex] == h, "Dense handle mismatch (internal corruption).");

		const uint32 lastIndex = static_cast<uint32>(m_Lights.size() - 1);
		if (denseIndex != lastIndex)
		{
			m_Lights[denseIndex] = std::move(m_Lights[lastIndex]);

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

		m_Lights.pop_back();
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

		m_Lights[denseIndex] = light;
	}

} // namespace shz
