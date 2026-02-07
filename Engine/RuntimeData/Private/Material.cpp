#include "pch.h"
#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	// Scalars (float)
	void Material::SetRawValue(const std::string& name, MATERIAL_VALUE_TYPE type, const void* data, uint32 byteSize)
	{
		ASSERT(type != MATERIAL_VALUE_TYPE_UNKNOWN, "Invalid value type.");
		ASSERT(data, "data is null.");
		ASSERT(byteSize > 0, "byteSize must be > 0.");
		ASSERT(byteSize == ValueTypeByteSize(type), "byteSize does not match expected size for type.");

		MaterialValueBlob& v = m_Values[name];
		v.Type = type;
		v.Data.resize(byteSize);
		std::memcpy(v.Data.data(), data, byteSize);
	}

	void Material::SetFloat(const std::string& name, float v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT, &v, sizeof(float));
	}

	void Material::SetFloat2(const std::string& name, const float2& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT2, &v, sizeof(float2));
	}

	void Material::SetFloat2(const std::string& name, const float v[2])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT2, v, sizeof(float2));
	}

	void Material::SetFloat3(const std::string& name, const float3& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT3, &v, sizeof(float3));
	}

	void Material::SetFloat3(const std::string& name, const float v[3])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT3, v, sizeof(float3));
	}

	void Material::SetFloat4(const std::string& name, const float4& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT4, &v, sizeof(float4));
	}

	void Material::SetFloat4(const std::string& name, const float v[4])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_FLOAT4, v, sizeof(float4));
	}

	void Material::SetInt(const std::string& name, int v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT, &v, sizeof(int));
	}

	void Material::SetInt2(const std::string& name, const int2& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT2, &v, sizeof(int2));
	}

	void Material::SetInt2(const std::string& name, const int v[2])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT2, v, sizeof(int2));
	}

	void Material::SetInt3(const std::string& name, const int3& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT3, &v, sizeof(int3));
	}

	void Material::SetInt3(const std::string& name, const int v[3])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT3, v, sizeof(int3));
	}

	void Material::SetInt4(const std::string& name, const int4& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT4, &v, sizeof(int4));
	}

	void Material::SetInt4(const std::string& name, const int v[4])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_INT4, v, sizeof(int4));
	}

	void Material::SetUint(const std::string& name, uint v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT, &v, sizeof(uint));
	}

	void Material::SetUint2(const std::string& name, const uint2& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT2, &v, sizeof(uint2));
	}

	void Material::SetUint2(const std::string& name, const uint v[2])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT2, v, sizeof(uint2));
	}

	void Material::SetUint3(const std::string& name, const uint3& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT3, &v, sizeof(uint3));
	}

	void Material::SetUint3(const std::string& name, const uint v[3])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT3, v, sizeof(uint3));
	}

	void Material::SetUint4(const std::string& name, const uint4& v)
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT4, &v, sizeof(uint4));
	}

	void Material::SetUint4(const std::string& name, const uint v[4])
	{
		return SetRawValue(name, MATERIAL_VALUE_TYPE_UINT4, v, sizeof(uint4));
	}

	void Material::SetTexture(const std::string& name, const AssetRef<Texture>& tex)
	{
		ASSERT(tex.IsValid(), "Invalid texture ref.");
		MaterialTexture& matTex = m_Textures[name];
		matTex.Texture = tex;
	}

	const MaterialValueBlob& Material::GetValue(const std::string& name) const
	{
		const auto it = m_Values.find(name);
		ASSERT(it != m_Values.end(), "Material value not found: %s", name.c_str());
		return it->second;
	}

	const MaterialValueBlob* Material::GetValueOrNull(const std::string& name) const noexcept
	{
		const auto it = m_Values.find(name);
		if (it != m_Values.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	const MaterialTexture& Material::GetTexture(const std::string& name) const
	{
		const auto it = m_Textures.find(name);
		ASSERT(it != m_Textures.end(), "Material texture not found: %s", name.c_str());
		return it->second;
	}

	const MaterialTexture* Material::GetTextureOrNull(const std::string& name) const noexcept
	{
		const auto it = m_Textures.find(name);
		if (it != m_Textures.end())
		{
			return &it->second;
		}
		return nullptr;
	}
} // namespace shz
