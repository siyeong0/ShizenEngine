#include "pch.h"
#include "RenderScene.h"

namespace shz
{
	// ------------------------------------------------------------
	// Slot helpers
	// ------------------------------------------------------------

	RenderScene::Slot<RenderScene::RenderObject>* RenderScene::FindSlot(Handle<RenderObject> h, std::vector<Slot<RenderObject>>& slots) noexcept
	{
		if (!h.IsValid())
		{
			return nullptr;
		}

		if (!Handle<RenderObject>::IsAlive(h))
		{
			return nullptr;
		}

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
		{
			return nullptr;
		}

		auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
		{
			return nullptr;
		}

		if (!slot.Value.has_value())
		{
			return nullptr;
		}

		return &slot;
	}

	const RenderScene::Slot<RenderScene::RenderObject>* RenderScene::FindSlot(Handle<RenderObject> h, const std::vector<Slot<RenderObject>>& slots) noexcept
	{
		if (!h.IsValid())
		{
			return nullptr;
		}

		if (!Handle<RenderObject>::IsAlive(h))
		{
			return nullptr;
		}

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
		{
			return nullptr;
		}

		const auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
		{
			return nullptr;
		}

		if (!slot.Value.has_value())
		{
			return nullptr;
		}

		return &slot;
	}

	RenderScene::Slot<RenderScene::LightObject>* RenderScene::FindSlot(Handle<LightObject> h, std::vector<Slot<LightObject>>& slots) noexcept
	{
		if (!h.IsValid())
		{
			return nullptr;
		}

		if (!Handle<LightObject>::IsAlive(h))
		{
			return nullptr;
		}

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
		{
			return nullptr;
		}

		auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
		{
			return nullptr;
		}

		if (!slot.Value.has_value())
		{
			return nullptr;
		}

		return &slot;
	}

	const RenderScene::Slot<RenderScene::LightObject>* RenderScene::FindSlot(Handle<LightObject> h, const std::vector<Slot<LightObject>>& slots) noexcept
	{
		if (!h.IsValid())
		{
			return nullptr;
		}

		if (!Handle<LightObject>::IsAlive(h))
		{
			return nullptr;
		}

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
		{
			return nullptr;
		}

		const auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
		{
			return nullptr;
		}

		if (!slot.Value.has_value())
		{
			return nullptr;
		}

		return &slot;
	}

	// ------------------------------------------------------------
	// Public API
	// ------------------------------------------------------------

	void RenderScene::Reset()
	{
		for (auto& s : m_ObjectSlots)
		{
			s.Value.reset();
			s.Owner.Reset();
		}

		for (auto& s : m_LightSlots)
		{
			s.Value.reset();
			s.Owner.Reset();
		}

		m_ObjectHandles.clear();
		m_LightHandles.clear();

		m_ObjectCount = 0;
		m_LightCount = 0;
	}

	Handle<RenderScene::RenderObject> RenderScene::AddObject(Handle<StaticMeshRenderData> meshHandle, const Matrix4x4& transform)
	{
		UniqueHandle<RenderObject> owner = UniqueHandle<RenderObject>::Make();
		const Handle<RenderObject> h = owner.Get();

		const uint32 index = h.GetIndex();
		EnsureSlotCapacity<Handle<RenderObject>>(index, m_ObjectSlots);

		auto& slot = m_ObjectSlots[index];
		ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "RenderObject slot already occupied.");

		RenderObject obj = {};
		obj.MeshHandle = meshHandle;
		obj.Transform = transform;

		slot.Owner = std::move(owner);
		slot.Value.emplace(std::move(obj));

		m_ObjectHandles.push_back(h);
		++m_ObjectCount;

		return h;
	}

	bool RenderScene::RemoveObject(Handle<RenderObject> h)
	{
		auto* slot = FindSlot(h, m_ObjectSlots);
		if (!slot)
		{
			return false;
		}

		slot->Value.reset();
		slot->Owner.Reset();

		--m_ObjectCount;

		// Optional: keep handle list compact by removing the entry
		for (size_t i = 0; i < m_ObjectHandles.size(); ++i)
		{
			if (m_ObjectHandles[i] == h)
			{
				m_ObjectHandles[i] = m_ObjectHandles.back();
				m_ObjectHandles.pop_back();
				break;
			}
		}

		return true;
	}

	bool RenderScene::SetObjectTransform(Handle<RenderObject> h, const Matrix4x4& world)
	{
		auto* slot = FindSlot(h, m_ObjectSlots);
		if (!slot)
		{
			return false;
		}

		slot->Value->Transform = world;
		return true;
	}

	bool RenderScene::SetObjectMesh(Handle<RenderObject> h, Handle<StaticMeshRenderData> mesh)
	{
		auto* slot = FindSlot(h, m_ObjectSlots);
		if (!slot)
		{
			return false;
		}

		slot->Value->MeshHandle = mesh;
		return true;
	}

	Handle<RenderScene::LightObject> RenderScene::AddLight(const LightObject& light)
	{
		UniqueHandle<LightObject> owner = UniqueHandle<LightObject>::Make();
		const Handle<LightObject> h = owner.Get();

		const uint32 index = h.GetIndex();
		EnsureSlotCapacity<Handle<LightObject>>(index, m_LightSlots);

		auto& slot = m_LightSlots[index];
		ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "LightObject slot already occupied.");

		slot.Owner = std::move(owner);
		slot.Value.emplace(light);

		m_LightHandles.push_back(h);
		++m_LightCount;

		return h;
	}

	bool RenderScene::RemoveLight(Handle<LightObject> h)
	{
		auto* slot = FindSlot(h, m_LightSlots);
		if (!slot)
		{
			return false;
		}

		slot->Value.reset();
		slot->Owner.Reset();

		--m_LightCount;

		for (size_t i = 0; i < m_LightHandles.size(); ++i)
		{
			if (m_LightHandles[i] == h)
			{
				m_LightHandles[i] = m_LightHandles.back();
				m_LightHandles.pop_back();
				break;
			}
		}

		return true;
	}

	bool RenderScene::UpdateLight(Handle<LightObject> h, const LightObject& light)
	{
		auto* slot = FindSlot(h, m_LightSlots);
		if (!slot)
		{
			return false;
		}

		*slot->Value = light;
		return true;
	}

	const RenderScene::RenderObject* RenderScene::TryGetObject(Handle<RenderObject> h) const noexcept
	{
		const auto* slot = FindSlot(h, m_ObjectSlots);
		if (!slot)
		{
			return nullptr;
		}

		return &slot->Value.value();
	}

	const RenderScene::LightObject* RenderScene::TryGetLight(Handle<LightObject> h) const noexcept
	{
		const auto* slot = FindSlot(h, m_LightSlots);
		if (!slot)
		{
			return nullptr;
		}

		return &slot->Value.value();
	}
} // namespace shz
