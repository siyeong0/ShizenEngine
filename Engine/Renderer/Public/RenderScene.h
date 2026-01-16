#pragma once
#include <vector>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/Core/Math/Math.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"

namespace shz
{
	class RenderScene final
	{
	public:
		struct RenderObject final
		{
			Handle<StaticMeshRenderData> MeshHandle = {};
			Matrix4x4 Transform = {};
		};

		struct LightObject final
		{
			uint32 Type = 0; // TODO: replace with enum
			float3  Color = { 1.0f, 1.0f, 1.0f };
			float  Intensity = 1.0f;

			float3  Position = { 0.0f, 0.0f, 0.0f };
			float3  Direction = { 0.0f, -1.0f, 0.0f };

			float  Range = 10.0f;
			float  SpotAngle = 30.0f;

			bool   CastShadow = false;
		};

	public:
		RenderScene() = default;
		RenderScene(const RenderScene&) = delete;
		RenderScene& operator=(const RenderScene&) = delete;
		~RenderScene() = default;

		void Reset();

		Handle<RenderObject> AddObject(Handle<StaticMeshRenderData> meshHandle, const Matrix4x4& transform);
		bool RemoveObject(Handle<RenderObject> h);

		bool SetObjectTransform(Handle<RenderObject> h, const Matrix4x4& world);
		bool SetObjectMesh(Handle<RenderObject> h, Handle<StaticMeshRenderData> mesh);

		Handle<LightObject> AddLight(const LightObject& light);
		bool RemoveLight(Handle<LightObject> h);
		bool UpdateLight(Handle<LightObject> h, const LightObject& light);

		uint32 GetObjectCount() const noexcept { return m_ObjectCount; }
		uint32 GetLightCount()  const noexcept { return m_LightCount; }

		const std::vector<Handle<RenderObject>>& GetObjectHandles() const noexcept { return m_ObjectHandles; }
		const std::vector<Handle<LightObject>>& GetLightHandles() const noexcept { return m_LightHandles; }

		const RenderObject* TryGetObject(Handle<RenderObject> h) const noexcept;
		const LightObject* TryGetLight(Handle<LightObject> h) const noexcept;

	private:
		template<typename T>
		struct Slot final
		{
			UniqueHandle<T> Owner = {};     // Owns handle lifetime (Destroy on reset/removal)
			std::optional<T> Value = {};    // Owns object lifetime (no assignment required)
		};

	private:
		static uint32 ToIndex(Handle<RenderObject> h) noexcept { return h.GetIndex(); }
		static uint32 ToIndex(Handle<LightObject>  h) noexcept { return h.GetIndex(); }

		template<typename THandle, typename TSlotVec>
		static void EnsureSlotCapacity(uint32 index, TSlotVec& slots)
		{
			if (index >= static_cast<uint32>(slots.size()))
			{
				slots.resize(static_cast<size_t>(index) + 1);
			}
		}

		static Slot<RenderObject>* FindSlot(Handle<RenderObject> h, std::vector<Slot<RenderObject>>& slots) noexcept;
		static const Slot<RenderObject>* FindSlot(Handle<RenderObject> h, const std::vector<Slot<RenderObject>>& slots) noexcept;

		static Slot<LightObject>* FindSlot(Handle<LightObject> h, std::vector<Slot<LightObject>>& slots) noexcept;
		static const Slot<LightObject>* FindSlot(Handle<LightObject> h, const std::vector<Slot<LightObject>>& slots) noexcept;

	private:
		std::vector<Slot<RenderObject>> m_ObjectSlots;
		std::vector<Handle<RenderObject>> m_ObjectHandles;
		uint32 m_ObjectCount = 0;

		std::vector<Slot<LightObject>> m_LightSlots;
		std::vector<Handle<LightObject>> m_LightHandles;
		uint32 m_LightCount = 0;
	};

} // namespace shz
