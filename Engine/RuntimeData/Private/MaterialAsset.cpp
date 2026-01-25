#include "pch.h"
#include "Engine/RuntimeData/Public/MaterialAsset.h"

#include "Engine/Material/Public/MaterialInstance.h"

namespace shz
{
	// ------------------------------------------------------------
	// Small helpers
	// ------------------------------------------------------------
	static inline bool strEq(const std::string& a, const char* b)
	{
		return (b != nullptr) ? (a == b) : false;
	}

	static inline void copyBytes(std::vector<uint8>& dst, const void* pData, uint32 byteSize)
	{
		ASSERT(byteSize > 0, "Byte size must not be 0.");
		dst.resize(byteSize);

		if (!pData)
		{
			std::memset(dst.data(), 0, byteSize);
			return;
		}

		std::memcpy(dst.data(), pData, byteSize);
	}

	// ============================================================
	// Lookups
	// ============================================================
	const MaterialAsset::ValueOverride* MaterialAsset::FindValueOverride(const char* name) const
	{
		ASSERT(name && name[0] != '\0', "Invalid name string.");

		for (const ValueOverride& v : m_ValueOverrides)
		{
			if (strEq(v.Name, name))
			{
				return &v;
			}
		}

		return nullptr;
	}

	MaterialAsset::ValueOverride* MaterialAsset::findValueOverrideMutable(const char* name)
	{
		ASSERT(name && name[0] != '\0', "Invalid name string.");

		for (ValueOverride& v : m_ValueOverrides)
		{
			if (strEq(v.Name, name))
			{
				return &v;
			}
		}

		return nullptr;
	}

	bool MaterialAsset::RemoveValueOverride(const char* name)
	{
		ASSERT(name && name[0] != '\0', "Invalid name string.");

		for (size_t i = 0; i < m_ValueOverrides.size(); ++i)
		{
			if (strEq(m_ValueOverrides[i].Name, name))
			{
				m_ValueOverrides.erase(m_ValueOverrides.begin() + i);
				return true;
			}
		}

		return false;
	}

	const MaterialAsset::ResourceBinding* MaterialAsset::FindResourceBinding(const char* name) const
	{
		ASSERT(name && name[0] != '\0', "Invalid name string.");

		for (const ResourceBinding& r : m_ResourceBindings)
		{
			if (strEq(r.Name, name))
			{
				return &r;
			}
		}

		return nullptr;
	}

	MaterialAsset::ResourceBinding* MaterialAsset::findResourceBindingMutable(const char* name)
	{
		ASSERT(name && name[0] != '\0', "Invalid name string.");

		for (ResourceBinding& r : m_ResourceBindings)
		{
			if (strEq(r.Name, name))
			{
				return &r;
			}
		}

		return nullptr;
	}

	bool MaterialAsset::RemoveResourceBinding(const char* name)
	{
		ASSERT(name && name[0] != '\0', "Invalid name string.");

		for (size_t i = 0; i < m_ResourceBindings.size(); ++i)
		{
			if (strEq(m_ResourceBindings[i].Name, name))
			{
				m_ResourceBindings.erase(m_ResourceBindings.begin() + i);
				return true;
			}
		}

		return false;
	}

	// ============================================================
	// Values
	// ============================================================
	bool MaterialAsset::SetFloat(const char* name, float v, uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_FLOAT, &v, sizeof(v), stableId);
	}

	bool MaterialAsset::SetFloat2(const char* name, const float v[2], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_FLOAT2, v, sizeof(float) * 2, stableId);
	}

	bool MaterialAsset::SetFloat3(const char* name, const float v[3], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_FLOAT3, v, sizeof(float) * 3, stableId);
	}

	bool MaterialAsset::SetFloat4(const char* name, const float v[4], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_FLOAT4, v, sizeof(float) * 4, stableId);
	}

	bool MaterialAsset::SetInt(const char* name, int32 v, uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_INT, &v, sizeof(v), stableId);
	}

	bool MaterialAsset::SetInt2(const char* name, const int32 v[2], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_INT2, v, sizeof(int32) * 2, stableId);
	}

	bool MaterialAsset::SetInt3(const char* name, const int32 v[3], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_INT3, v, sizeof(int32) * 3, stableId);
	}

	bool MaterialAsset::SetInt4(const char* name, const int32 v[4], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_INT4, v, sizeof(int32) * 4, stableId);
	}

	bool MaterialAsset::SetUint(const char* name, uint32 v, uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_UINT, &v, sizeof(v), stableId);
	}

	bool MaterialAsset::SetUint2(const char* name, const uint32 v[2], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_UINT2, v, sizeof(uint32) * 2, stableId);
	}

	bool MaterialAsset::SetUint3(const char* name, const uint32 v[3], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_UINT3, v, sizeof(uint32) * 3, stableId);
	}

	bool MaterialAsset::SetUint4(const char* name, const uint32 v[4], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_UINT4, v, sizeof(uint32) * 4, stableId);
	}

	bool MaterialAsset::SetFloat4x4(const char* name, const float m16[16], uint64 stableId)
	{
		return writeValueInternal(name, MATERIAL_VALUE_TYPE_FLOAT4X4, m16, sizeof(float) * 16, stableId);
	}

	bool MaterialAsset::SetRaw(
		const char* name,
		MATERIAL_VALUE_TYPE type,
		const void* pData,
		uint32 byteSize,
		uint64 stableId)
	{
		return writeValueInternal(name, type, pData, byteSize, stableId);
	}

	bool MaterialAsset::writeValueInternal(
		const char* name,
		MATERIAL_VALUE_TYPE type,
		const void* pData,
		uint32 byteSize,
		uint64 stableId)
	{
		ASSERT(name && name[0] != '\0', "Invalid name string.");
		ASSERT(type != MATERIAL_VALUE_TYPE_UNKNOWN, "Value type is unkown. Please specify value type.");
		ASSERT(byteSize > 0, "Byte size must not be 0.");

		ValueOverride* v = findValueOverrideMutable(name);
		if (!v)
		{
			ValueOverride nv = {};
			nv.StableID = stableId;
			nv.Name = name;
			nv.Type = type;
			copyBytes(nv.Data, pData, byteSize);
			m_ValueOverrides.push_back(static_cast<ValueOverride&&>(nv));
			return true;
		}

		v->StableID = (stableId != 0) ? stableId : v->StableID;
		v->Type = type;
		copyBytes(v->Data, pData, byteSize);
		return true;
	}

	// ============================================================
	// Resources
	// ============================================================
	bool MaterialAsset::SetTextureAssetRef(
		const char* resourceName,
		MATERIAL_RESOURCE_TYPE expectedType,
		const AssetRef<TextureAsset>& textureRef,
		uint64 stableId)
	{
		ASSERT(resourceName && resourceName[0] != '\0', "Invalid name string.");
		ASSERT(IsTextureType(expectedType), "Expected type must be a texture type.");

		ResourceBinding* r = findResourceBindingMutable(resourceName);
		if (!r)
		{
			ResourceBinding nr = {};
			nr.StableID = stableId;
			nr.Name = resourceName;
			nr.Type = expectedType;
			nr.TextureRef = textureRef;
			m_ResourceBindings.push_back(static_cast<ResourceBinding&&>(nr));
			return true;
		}

		r->StableID = (stableId != 0) ? stableId : r->StableID;
		r->Type = expectedType;
		r->TextureRef = textureRef;
		return true;
	}

	bool MaterialAsset::SetSamplerOverride(
		const char* resourceName,
		const SamplerDesc& desc,
		uint64 stableId)
	{
		ASSERT(resourceName && resourceName[0] != '\0', "Invalid name string.");

		ResourceBinding* r = findResourceBindingMutable(resourceName);
		if (!r)
		{
			ResourceBinding nr = {};
			nr.StableID = stableId;
			nr.Name = resourceName;
			nr.bHasSamplerOverride = true;
			nr.SamplerOverrideDesc = desc;
			m_ResourceBindings.push_back(static_cast<ResourceBinding&&>(nr));
			return true;
		}

		r->StableID = (stableId != 0) ? stableId : r->StableID;
		r->bHasSamplerOverride = true;
		r->SamplerOverrideDesc = desc;
		return true;
	}

	bool MaterialAsset::ClearSamplerOverride(const char* resourceName)
	{
		ASSERT(resourceName && resourceName[0] != '\0', "Invalid name string.");

		ResourceBinding* r = findResourceBindingMutable(resourceName);
		if (!r)
		{
			return false;
		}

		r->bHasSamplerOverride = false;
		return true;
	}

	// ============================================================
	// Reset
	// ============================================================
	void MaterialAsset::Clear()
	{
		m_Name.clear();
		m_TemplateName.clear();

		m_Options = {};

		m_ValueOverrides.clear();
		m_ResourceBindings.clear();
	}

	// ============================================================
	// Apply to runtime instance
	// ============================================================
	bool MaterialAsset::ApplyToInstance(MaterialInstance* pInstance) const
	{
		ASSERT(pInstance, "Material instance is null.");

		// Options -> instance knobs
		pInstance->SetRenderPass(m_RenderPassName);
		pInstance->SetCullMode(m_Options.CullMode);
		pInstance->SetFrontCounterClockwise(m_Options.FrontCounterClockwise);

		pInstance->SetDepthEnable(m_Options.DepthEnable);
		pInstance->SetDepthWriteEnable(m_Options.DepthWriteEnable);
		pInstance->SetDepthFunc(m_Options.DepthFunc);

		pInstance->SetTextureBindingMode(m_Options.TextureBindingMode);

		// Values
		for (const ValueOverride& v : m_ValueOverrides)
		{
			if (v.Name.empty() || v.Data.empty())
			{
				continue;
			}

			pInstance->SetRaw(v.Name.c_str(), v.Data.data(), static_cast<uint32>(v.Data.size()));
		}

		// Resources
		for (const ResourceBinding& r : m_ResourceBindings)
		{
			if (r.Name.empty())
			{
				continue;
			}

			if (r.TextureRef)
			{
				pInstance->SetTextureAsset(r.Name.c_str(), r.TextureRef);
			}

			if (r.bHasSamplerOverride)
			{
				// MaterialInstance currently supports per-texture sampler override via ISampler*,
				// but the asset stores SamplerDesc. If you have a sampler cache, resolve it there.
				// For now, just store it in the asset and let higher-level code apply it.
				// (No-op here.)
				ASSERT(false, "Not implemented.");
			}
		}

		return true;
	}

} // namespace shz
