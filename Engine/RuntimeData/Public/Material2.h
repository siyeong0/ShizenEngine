#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <cstdint>
#include <cstring>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/HashUtils.hpp"
#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/RuntimeData/Public/Texture.h"
#include "Engine/RuntimeData/Public/MaterialTypes.h"

namespace shz
{
	using Material2Id = uint64;
	using Material2ParamId = uint64;

	struct MaterialTexture final
	{
		AssetRef<Texture> Texture;
	};

	struct MaterialValueBlob final
	{
		MATERIAL_VALUE_TYPE Type = MATERIAL_VALUE_TYPE_UNKNOWN;
		std::vector<uint8> Data = {};
	};

	// ---------------------------------------------------------------------
	class Material2 final
	{
	public:
		Material2() = default;

		explicit Material2(const std::string& name) : m_Name(name) {}

		const std::string& GetName() const noexcept { return m_Name; }

		// Options
		void SetBlendMode(MATERIAL_BLEND_MODE blendMode) { m_BlendMode = blendMode; }
		void SetCullMode(CULL_MODE cullMode) { m_CullMode = cullMode; }
		void SetCCW(bool bCCW) { m_bFrontCounterClockWise = bCCW; }
		void SetDepthTestEnable(bool bDepthTestEnable) { m_bDepthEnalbe = bDepthTestEnable; }
		void SetDepthWriteEnable(bool bDepthWriteEnable) { m_bDepthWriteEnable = bDepthWriteEnable; }
		void SetDepthComparisonFunc(COMPARISON_FUNCTION func) { m_DepthFunc = func; }

		// Scalars (float)
		void SetRawValue(const std::string& name, MATERIAL_VALUE_TYPE type, const void* data, uint32 byteSize);

		void SetFloat(const std::string& name, float v);
		void SetFloat2(const std::string& name, const float2& v);
		void SetFloat2(const std::string& name, const float v[2]);
		void SetFloat3(const std::string& name, const float3& v);
		void SetFloat3(const std::string& name, const float v[3]);
		void SetFloat4(const std::string& name, const float4& v);
		void SetFloat4(const std::string& name, const float v[4]);

		void SetInt(const std::string& name, int v);
		void SetInt2(const std::string& name, const int2& v);
		void SetInt2(const std::string& name, const int v[2]);
		void SetInt3(const std::string& name, const int3& v);
		void SetInt3(const std::string& name, const int v[3]);
		void SetInt4(const std::string& name, const int4& v);
		void SetInt4(const std::string& name, const int v[4]);

		void SetUint(const std::string& name, uint v);
		void SetUint2(const std::string& name, const uint2& v);
		void SetUint2(const std::string& name, const uint v[2]);
		void SetUint3(const std::string& name, const uint3& v);
		void SetUint3(const std::string& name, const uint v[3]);
		void SetUint4(const std::string& name, const uint4& v);
		void SetUint4(const std::string& name, const uint v[4]);

		// Textures
		void SetTexture(const std::string& name, const AssetRef<Texture>& tex);

	private:
		std::string  m_Name = {};

		MATERIAL_BLEND_MODE m_BlendMode = MATERIAL_BLEND_MODE_OPAQUE;
		CULL_MODE m_CullMode = CULL_MODE_BACK;
		bool m_bFrontCounterClockWise = true;

		bool m_bDepthEnalbe = true;
		bool m_bDepthWriteEnable = true;
		COMPARISON_FUNCTION m_DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		std::unordered_map<Material2ParamId, MaterialValueBlob> m_Values = {};
		std::unordered_map<Material2ParamId, MaterialTexture> m_Textures = {};
	};
} // namespace shz
