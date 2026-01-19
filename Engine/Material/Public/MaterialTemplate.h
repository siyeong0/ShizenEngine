#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IShaderResourceVariable.h"

namespace shz
{
	enum MATERIAL_VALUE_TYPE : uint8
	{
		MATERIAL_VALUE_TYPE_UNKNOWN = 0,

		MATERIAL_VALUE_TYPE_FLOAT,
		MATERIAL_VALUE_TYPE_FLOAT2,
		MATERIAL_VALUE_TYPE_FLOAT3,
		MATERIAL_VALUE_TYPE_FLOAT4,

		MATERIAL_VALUE_TYPE_INT,
		MATERIAL_VALUE_TYPE_INT2,
		MATERIAL_VALUE_TYPE_INT3,
		MATERIAL_VALUE_TYPE_INT4,

		MATERIAL_VALUE_TYPE_UINT,
		MATERIAL_VALUE_TYPE_UINT2,
		MATERIAL_VALUE_TYPE_UINT3,
		MATERIAL_VALUE_TYPE_UINT4,

		MATERIAL_VALUE_TYPE_FLOAT4X4,
	};

	enum MATERIAL_RESOURCE_TYPE : uint8
	{
		MATERIAL_RESOURCE_TYPE_UNKNOWN = 0,

		MATERIAL_RESOURCE_TYPE_TEXTURE2D,
		MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY,
		MATERIAL_RESOURCE_TYPE_TEXTURECUBE,

		MATERIAL_RESOURCE_TYPE_STRUCTUREDBUFFER,
		MATERIAL_RESOURCE_TYPE_RWSTRUCTUREDBUFFER,
	};

	enum MaterialParamFlags : uint32
	{
		MaterialParamFlags_None = 0,
		MaterialParamFlags_Hidden = 1u << 0u,
		MaterialParamFlags_ReadOnly = 1u << 1u,
		MaterialParamFlags_PerInstance = 1u << 2u,
	};
	DEFINE_FLAG_ENUM_OPERATORS(MaterialParamFlags);

	struct MaterialValueParamDesc final
	{
		std::string Name = {};
		MATERIAL_VALUE_TYPE Type = MATERIAL_VALUE_TYPE_UNKNOWN;

		uint16 CBufferIndex = 0;
		uint16 ByteOffset = 0;
		uint16 ByteSize = 0;

		MaterialParamFlags Flags = MaterialParamFlags_None;
	};

	struct MaterialCBufferDesc final
	{
		std::string Name = {};
		uint32 ByteSize = 0;
		bool IsDynamic = true; // MATERIAL_CONSTANTS´Â º¸Åë dynamic
	};

	struct MaterialResourceDesc final
	{
		std::string Name = {};
		MATERIAL_RESOURCE_TYPE Type = MATERIAL_RESOURCE_TYPE_UNKNOWN;
		uint16 ArraySize = 1;
		bool IsDynamic = true;
	};

	class MaterialTemplate final
	{
	public:
		MaterialTemplate() = default;
		~MaterialTemplate() = default;

		MaterialTemplate(const MaterialTemplate&) = delete;
		MaterialTemplate& operator=(const MaterialTemplate&) = delete;

		MaterialTemplate(MaterialTemplate&&) noexcept = default;
		MaterialTemplate& operator=(MaterialTemplate&&) noexcept = default;

		// Build from shader reflection.
		// Policy:
		// - Only one constant buffer is reflected: "MATERIAL_CONSTANTS"
		// - Value param names are "Var" (no "CB.Var" prefix)
		bool BuildFromShaders(const std::vector<const IShader*>& pShaders);

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		// Value params
		uint32 GetValueParamCount() const { return static_cast<uint32>(m_ValueParams.size()); }
		const MaterialValueParamDesc& GetValueParam(uint32 index) const { return m_ValueParams[index]; }
		const MaterialValueParamDesc* FindValueParam(const char* name) const;
		bool FindValueParamIndex(const char* name, uint32& outIndex) const;

		// Constant buffers
		uint32 GetCBufferCount() const { return static_cast<uint32>(m_CBuffers.size()); }
		const MaterialCBufferDesc& GetCBuffer(uint32 index) const { return m_CBuffers[index]; }

		// Resources
		uint32 GetResourceCount() const { return static_cast<uint32>(m_Resources.size()); }
		const MaterialResourceDesc& GetResource(uint32 index) const { return m_Resources[index]; }
		const MaterialResourceDesc* FindResource(const char* name) const;
		bool FindResourceIndex(const char* name, uint32& outIndex) const;

		bool ValidateSetValue(const char* name, MATERIAL_VALUE_TYPE expectedType, MaterialValueParamDesc* pOutDesc = nullptr) const;
		bool ValidateSetResource(const char* name, MATERIAL_RESOURCE_TYPE expectedType, MaterialResourceDesc* pOutDesc = nullptr) const;

		static constexpr const char* MATERIAL_CBUFFER_NAME = "MATERIAL_CONSTANTS";

	private:
		std::string m_Name = {};
		std::vector<const IShader*> m_pShaders = {};

		std::unordered_map<std::string, uint32> m_ValueParamLut = {};
		std::unordered_map<std::string, uint32> m_ResourceLut = {};

		std::vector<MaterialCBufferDesc> m_CBuffers = {};
		std::vector<MaterialValueParamDesc> m_ValueParams = {};
		std::vector<MaterialResourceDesc> m_Resources = {};
	};

} // namespace shz
