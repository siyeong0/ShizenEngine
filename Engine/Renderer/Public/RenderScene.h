#pragma once
#include <vector>
#include <cstddef>
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

namespace shz
{
	struct MeshHandle { uint32 Id = 0; };
	struct MaterialHandle { uint32 Id = 0; };
	struct RenderObjectId { uint32 Id = 0; };
	struct LightId { uint32 Id = 0; };

	inline constexpr bool operator==(MeshHandle a, MeshHandle b) { return a.Id == b.Id; }
	inline constexpr bool operator==(MaterialHandle a, MaterialHandle b) { return a.Id == b.Id; }
	inline constexpr bool operator==(RenderObjectId a, RenderObjectId b) { return a.Id == b.Id; }
	inline constexpr bool operator==(LightId a, LightId b) { return a.Id == b.Id; }
} // namespace shz

namespace std
{
	template<> struct hash<shz::MeshHandle>
	{
		size_t operator()(const shz::MeshHandle& h) const noexcept { return std::hash<shz::uint32>{}(h.Id); }
	};
	template<> struct hash<shz::MaterialHandle>
	{
		size_t operator()(const shz::MaterialHandle& h) const noexcept { return std::hash<shz::uint32>{}(h.Id); }
	};
	template<> struct hash<shz::RenderObjectId>
	{
		size_t operator()(const shz::RenderObjectId& h) const noexcept { return std::hash<shz::uint32>{}(h.Id); }
	};
	template<> struct hash<shz::LightId>
	{
		size_t operator()(const shz::LightId& h) const noexcept { return std::hash<shz::uint32>{}(h.Id); }
	};
}

namespace shz
{
	struct LightDesc
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

	class RenderScene
	{
	public:
		void Reset();

		RenderObjectId AddObject(const MeshHandle& meshHandle, const Matrix4x4& transform);
		void RemoveObject(RenderObjectId id);

		void SetObjectTransform(RenderObjectId id, const Matrix4x4& world);
		void SetObjectMesh(RenderObjectId id, MeshHandle mesh);

		LightId AddLight(const LightDesc& desc);
		void RemoveLight(LightId id);
		void UpdateLight(LightId id, const LightDesc& desc);

		uint32 GetObjectCount() const { return static_cast<uint32>(m_Objects.size()); }
		uint32 GetLightCount()  const { return static_cast<uint32>(m_Lights.size()); }

		struct RenderObject
		{
			MeshHandle meshHandle = {};
			Matrix4x4  transform = {};
		};

		const std::unordered_map<RenderObjectId, RenderObject>& GetObjects() const { return m_Objects; }

	private:
		uint32 m_NextObjectId = 1;
		uint32 m_NextLightId = 1;

		std::unordered_map<RenderObjectId, RenderObject> m_Objects;
		std::unordered_map<LightId, LightDesc>           m_Lights;
	};

} // namespace shz
