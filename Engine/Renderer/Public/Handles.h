#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
	struct TextureHandle 
	{ 
		uint32 Id = 0; 
		bool IsValid() const { return Id != 0; }
	};
	struct MeshHandle 
	{ 
		uint32 Id = 0;
		bool IsValid() const { return Id != 0; }
	};
	struct MaterialHandle
	{
		uint32 Id = 0; 
		bool IsValid() const { return Id != 0; }
	};
	struct RenderObjectId 
	{ 
		uint32 Id = 0; 
		bool IsValid() const { return Id != 0; }
	};
	struct LightId 
	{ 
		uint32 Id = 0;
		bool IsValid() const { return Id != 0; }
	};

	inline constexpr bool operator==(TextureHandle a, TextureHandle b) { return a.Id == b.Id; }
	inline constexpr bool operator==(MeshHandle a, MeshHandle b) { return a.Id == b.Id; }
	inline constexpr bool operator==(MaterialHandle a, MaterialHandle b) { return a.Id == b.Id; }
	inline constexpr bool operator==(RenderObjectId a, RenderObjectId b) { return a.Id == b.Id; }
	inline constexpr bool operator==(LightId a, LightId b) { return a.Id == b.Id; }
}

namespace std
{
	template<> struct hash<shz::TextureHandle>
	{
		size_t operator()(const shz::TextureHandle& h) const noexcept { return std::hash<shz::uint32>{}(h.Id); }
	};
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