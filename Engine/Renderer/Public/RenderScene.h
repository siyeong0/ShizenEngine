#pragma once
#include <vector>
#include <cstddef>
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Engine/Core/Math/Math.h"

namespace shz
{

	class RenderScene
	{
	public:
		struct RenderObject
		{
			Handle<StaticMeshRenderData> meshHandle = {};
			Matrix4x4  transform = {};
		};

		struct LightObject
		{
			uint32 Type = 0; // TODO enum
			float  Color[3] = { 1, 1, 1 };
			float  Intensity = 1.0f;

			float  Position[3] = { 0, 0, 0 };
			float  Direction[3] = { 0, -1, 0 };

			float  Range = 10.0f;
			float  SpotAngle = 30.0f;

			bool   CastShadow = false;
		};

	public:
		void Reset();

		Handle<RenderObject> AddObject(const Handle<StaticMeshRenderData>& meshHandle, const Matrix4x4& transform);
		void RemoveObject(Handle<RenderObject> id);

		void SetObjectTransform(Handle<RenderObject> id, const Matrix4x4& world);
		void SetObjectMesh(Handle<RenderObject> id, Handle<StaticMeshRenderData> mesh);

		Handle<LightObject> AddLight(const LightObject& light);
		void RemoveLight(Handle<LightObject> id);
		void UpdateLight(Handle<LightObject> id, const LightObject& light);

		uint32 GetObjectCount() const { return static_cast<uint32>(m_Objects.size()); }
		uint32 GetLightCount()  const { return static_cast<uint32>(m_Lights.size()); }

		const std::unordered_map<Handle<RenderObject>, RenderObject>& GetObjects() const { return m_Objects; }

	private:
		uint32 m_NextObjectId = 1;
		uint32 m_NextLightId = 1;

		std::unordered_map<Handle<RenderObject>, RenderObject> m_Objects;
		std::unordered_map<Handle<LightObject>, LightObject>           m_Lights;
	};

} // namespace shz
