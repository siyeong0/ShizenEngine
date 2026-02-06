#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include <nlohmann/json.hpp>

namespace shz
{
	// -----------------------------
	// Generic helpers (arrays)
	// -----------------------------
	template <typename TVec, typename T, int N>
	inline void VectorToJsonArray(nlohmann::json& j, const TVec& v)
	{
		j = nlohmann::json::array();
		for (int i = 0; i < N; ++i)
		{
			j.push_back(static_cast<T>(v[i]));
		}
	}

	template <typename TVec, typename T, int N>
	inline void VectorFromJsonArray(const nlohmann::json& j, TVec& v)
	{
		ASSERT(j.is_array(), "json is not an array.");
		ASSERT((int)j.size() == N, "json array size mismatch.");

		for (int i = 0; i < N; ++i)
		{
			v[i] = j[i].get<T>();
		}
	}
}

// ------------------------------------------------------------
// nlohmann::json ADL serializer specializations
// ------------------------------------------------------------
namespace nlohmann
{
	// float
	template <> struct adl_serializer<shz::float2>
	{
		static void to_json(json& j, const shz::float2& v) { shz::VectorToJsonArray<shz::float2, float, 2>(j, v); }
		static void from_json(const json& j, shz::float2& v) { shz::VectorFromJsonArray<shz::float2, float, 2>(j, v); }
	};

	template <> struct adl_serializer<shz::float3>
	{
		static void to_json(json& j, const shz::float3& v) { shz::VectorToJsonArray<shz::float3, float, 3>(j, v); }
		static void from_json(const json& j, shz::float3& v) { shz::VectorFromJsonArray<shz::float3, float, 3>(j, v); }
	};

	template <> struct adl_serializer<shz::float4>
	{
		static void to_json(json& j, const shz::float4& v) { shz::VectorToJsonArray<shz::float4, float, 4>(j, v); }
		static void from_json(const json& j, shz::float4& v) { shz::VectorFromJsonArray<shz::float4, float, 4>(j, v); }
	};

	// int
	template <> struct adl_serializer<shz::int2>
	{
		static void to_json(json& j, const shz::int2& v) { shz::VectorToJsonArray<shz::int2, int, 2>(j, v); }
		static void from_json(const json& j, shz::int2& v) { shz::VectorFromJsonArray<shz::int2, int, 2>(j, v); }
	};

	template <> struct adl_serializer<shz::int3>
	{
		static void to_json(json& j, const shz::int3& v) { shz::VectorToJsonArray<shz::int3, int, 3>(j, v); }
		static void from_json(const json& j, shz::int3& v) { shz::VectorFromJsonArray<shz::int3, int, 3>(j, v); }
	};

	template <> struct adl_serializer<shz::int4>
	{
		static void to_json(json& j, const shz::int4& v) { shz::VectorToJsonArray<shz::int4, int, 4>(j, v); }
		static void from_json(const json& j, shz::int4& v) { shz::VectorFromJsonArray<shz::int4, int, 4>(j, v); }
	};

	// uint
	template <> struct adl_serializer<shz::uint2>
	{
		static void to_json(json& j, const shz::uint2& v) { shz::VectorToJsonArray<shz::uint2, shz::uint32, 2>(j, v); }
		static void from_json(const json& j, shz::uint2& v) { shz::VectorFromJsonArray<shz::uint2, shz::uint32, 2>(j, v); }
	};

	template <> struct adl_serializer<shz::uint3>
	{
		static void to_json(json& j, const shz::uint3& v) { shz::VectorToJsonArray<shz::uint3, shz::uint32, 3>(j, v); }
		static void from_json(const json& j, shz::uint3& v) { shz::VectorFromJsonArray<shz::uint3, shz::uint32, 3>(j, v); }
	};

	template <> struct adl_serializer<shz::uint4>
	{
		static void to_json(json& j, const shz::uint4& v) { shz::VectorToJsonArray<shz::uint4, shz::uint32, 4>(j, v); }
		static void from_json(const json& j, shz::uint4& v) { shz::VectorFromJsonArray<shz::uint4, shz::uint32, 4>(j, v); }
	};

	// box
	template <> struct adl_serializer<shz::Box>
	{
		static void to_json(json& j, const shz::Box& v)
		{
			j = json::object();
			j["Min"] = v.Min;
			j["Max"] = v.Max;
		}
		static void from_json(const json& j, shz::Box& v)
		{
			v.Min = j.at("Min").get<shz::float3>();
			v.Max = j.at("Max").get<shz::float3>();
		}
	};
}
