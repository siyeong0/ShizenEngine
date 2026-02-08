#include "pch.h"
#include "Engine/RuntimeData/Public/Material2.h"

namespace shz
{
	// Scalars (float)
	void Material2::SetRawValue(const std::string& name, MATERIAL_VALUE_TYPE type, const void* data, uint32 byteSize)
	{
		ASSERT(type != MATERIAL_VALUE_TYPE_UNKNOWN, "Invalid value type.");
		ASSERT(data, "data is null.");
		ASSERT(byteSize > 0, "byteSize must be > 0.");

		const Material2ParamId id = STRING_HASH(name);

		MaterialValueBlob& v = m_Values[id];
		v.Type = type;
		v.Data.resize(byteSize);
		std::memcpy(v.Data.data(), data, byteSize);
	}

	void Material2::SetFloat(const std::string& name, float v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT, &v, sizeof(float));
	}

	void Material2::SetFloat2(const std::string& name, const float2& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT2, &v, sizeof(float2));
	}

	void Material2::SetFloat2(const std::string& name, const float v[2])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT2, v, sizeof(float2));
	}

	void Material2::SetFloat3(const std::string& name, const float3& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT3, &v, sizeof(float3));
	}

	void Material2::SetFloat3(const std::string& name, const float v[3])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT3, v, sizeof(float3));
	}

	void Material2::SetFloat4(const std::string& name, const float4& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT4, &v, sizeof(float4));
	}

	void Material2::SetFloat4(const std::string& name, const float v[4])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT4, v, sizeof(float4));
	}

	void Material2::SetInt(const std::string& name, int v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT, &v, sizeof(int));
	}

	void Material2::SetInt2(const std::string& name, const int2& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT2, &v, sizeof(int2));
	}

	void Material2::SetInt2(const std::string& name, const int v[2])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT2, v, sizeof(int2));
	}

	void Material2::SetInt3(const std::string& name, const int3& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT3, &v, sizeof(int3));
	}

	void Material2::SetInt3(const std::string& name, const int v[3])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT3, v, sizeof(int3));
	}

	void Material2::SetInt4(const std::string& name, const int4& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT4, &v, sizeof(int4));
	}

	void Material2::SetInt4(const std::string& name, const int v[4])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT4, v, sizeof(int4));
	}

	void Material2::SetUint(const std::string& name, uint v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT, &v, sizeof(uint));
	}

	void Material2::SetUint2(const std::string& name, const uint2& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT2, &v, sizeof(uint2));
	}

	void Material2::SetUint2(const std::string& name, const uint v[2])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT2, v, sizeof(uint2));
	}

	void Material2::SetUint3(const std::string& name, const uint3& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT3, &v, sizeof(uint3));
	}

	void Material2::SetUint3(const std::string& name, const uint v[3])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT3, v, sizeof(uint3));
	}

	void Material2::SetUint4(const std::string& name, const uint4& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT4, &v, sizeof(uint4));
	}

	void Material2::SetUint4(const std::string& name, const uint v[4])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT4, v, sizeof(uint4));
	}


	void Material2::SetTexture(const std::string& name, const AssetRef<Texture>& tex)
	{
		ASSERT(tex.IsValid(), "Invalid texture ref.");

		const Material2ParamId id = STRING_HASH(name);
		MaterialTexture& matTex = m_Textures[id];
	}
} // namespace shz
