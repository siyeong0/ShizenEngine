// RenderScene.h
#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/Core/Math/Math.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Material/Public/MaterialInstance.h"

namespace shz
{
	class RenderScene final
	{
	public:
		struct RenderObject final
		{
			Handle<StaticMeshRenderData> MeshHandle = {};
			std::vector<MaterialInstance> Materials = {};
			Matrix4x4 Transform = {};
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

	public:
		RenderScene() = default;
		RenderScene(const RenderScene&) = delete;
		RenderScene& operator=(const RenderScene&) = delete;
		~RenderScene() = default;

		void Reset();

		Handle<RenderObject> AddObject(Handle<StaticMeshRenderData> meshHandle, const std::vector<MaterialInstance>& materials, const Matrix4x4& transform);
		void RemoveObject(Handle<RenderObject> h);
		void UpdateObjectMesh(Handle<RenderObject> h, Handle<StaticMeshRenderData> mesh);
		void UpdateObjectMaterial(Handle<RenderObject> h, uint32 materialSlot, const MaterialInstance& material);
		void UpdateObjectMaterials(Handle<RenderObject> h, const std::vector<MaterialInstance>& materials);
		void UpdateObjectTransform(Handle<RenderObject> h, const Matrix4x4& world);

		Handle<LightObject> AddLight(const LightObject& light);
		void RemoveLight(Handle<LightObject> h);
		void UpdateLight(Handle<LightObject> h, const LightObject& light);

		uint32 GetObjectCount() const noexcept { return static_cast<uint32>(m_Objects.size()); }
		uint32 GetLightCount()  const noexcept { return static_cast<uint32>(m_Lights.size()); }

		// Dense arrays for fast per-frame iteration
		const std::vector<RenderObject>& GetObjects() const noexcept { return m_Objects; }
		const std::vector<LightObject>& GetLights()  const noexcept { return m_Lights; }

	private:
		static constexpr uint32 INVALID_INDEX = 0xFFFFFFFFu;

		template<typename T>
		struct Slot final
		{
			UniqueHandle<T> Owner = {}; // owns handle lifetime
			uint32 DenseIndex = INVALID_INDEX;
			bool bOccupied = false; // fast flag (avoid optional<T>)
		};

	private:
		static void ensureCapacity(uint32 index, std::vector<uint32>& v)
		{
			if (index >= static_cast<uint32>(v.size())) { v.resize(static_cast<size_t>(index) + 1024, INVALID_INDEX); }
		}

		template<typename T>
		static void ensureCapacity(uint32 index, std::vector<Slot<T>>& v)
		{
			if (index >= static_cast<uint32>(v.size())) { v.resize(static_cast<size_t>(index) + 1024); }
		}

		template<typename T>
		uint32 findDenseIndex(Handle<T> h, const std::vector<Slot<T>>& slots) const noexcept;

	private:
		// ------------------------------------------------------------
		// RenderObjects (Dense/Sparse set)
		// ------------------------------------------------------------
		std::vector<Slot<RenderObject>> m_ObjectSlots;        // indexed by Handle index
		std::vector<uint32> m_ObjectSparse;       // Handle index -> dense index
		std::vector<RenderObject> m_Objects;            // dense objects
		std::vector<Handle<RenderObject>> m_ObjectHandles;      // dense handles aligned with m_Objects

		// ------------------------------------------------------------
		// Lights (Dense/Sparse set)
		// ------------------------------------------------------------
		std::vector<Slot<LightObject>> m_LightSlots;
		std::vector<uint32> m_LightSparse;
		std::vector<LightObject> m_Lights;
		std::vector<Handle<LightObject>> m_LightHandles;
	};

} // namespace shz
