#pragma once
#include <cstdint>
#include <cstring>
#include <string>

#include "Primitives/BasicTypes.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/RHI/Interface/RasterizerState.h"
#include "Engine/RHI/Interface/ISampler.h"
#include "Engine/RHI/Interface/IShader.h"

namespace shz
{
	// Core material enums (shared by Template/Instance/Asset)

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

	enum MATERIAL_PIPELINE_TYPE : uint8
	{
		MATERIAL_PIPELINE_TYPE_UNKNOWN = 0,
		MATERIAL_PIPELINE_TYPE_GRAPHICS,
		MATERIAL_PIPELINE_TYPE_COMPUTE,
	};

	enum MATERIAL_TEXTURE_BINDING_MODE : uint8
	{
		MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC = 0,
		MATERIAL_TEXTURE_BINDING_MODE_MUTABLE,
	};

	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------
	static inline bool IsTextureType(MATERIAL_RESOURCE_TYPE t)
	{
		return (t == MATERIAL_RESOURCE_TYPE_TEXTURE2D) ||
			(t == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ||
			(t == MATERIAL_RESOURCE_TYPE_TEXTURECUBE);
	}

	static inline uint32 ValueTypeByteSize(MATERIAL_VALUE_TYPE t) noexcept
	{
		switch (t)
		{
		case MATERIAL_VALUE_TYPE_FLOAT:    return 4;
		case MATERIAL_VALUE_TYPE_FLOAT2:   return 8;
		case MATERIAL_VALUE_TYPE_FLOAT3:   return 12;
		case MATERIAL_VALUE_TYPE_FLOAT4:   return 16;

		case MATERIAL_VALUE_TYPE_INT:      return 4;
		case MATERIAL_VALUE_TYPE_INT2:     return 8;
		case MATERIAL_VALUE_TYPE_INT3:     return 12;
		case MATERIAL_VALUE_TYPE_INT4:     return 16;

		case MATERIAL_VALUE_TYPE_UINT:     return 4;
		case MATERIAL_VALUE_TYPE_UINT2:    return 8;
		case MATERIAL_VALUE_TYPE_UINT3:    return 12;
		case MATERIAL_VALUE_TYPE_UINT4:    return 16;

		case MATERIAL_VALUE_TYPE_FLOAT4X4: return 64;

		default:
			return 0;
		}
	}

	// ------------------------------------------------------------
	// Shared options (Asset/Instance)
	// - Asset: persistent authoring values
	// - Instance: runtime knobs driving PSO/layout dirty
	// ------------------------------------------------------------
	struct MaterialCommonOptions
	{
		// Raster
		CULL_MODE CullMode = CULL_MODE_BACK;
		bool FrontCounterClockwise = true;

		// Depth
		bool DepthEnable = true;
		bool DepthWriteEnable = true;
		COMPARISON_FUNCTION DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		// Texture resource variable type policy
		MATERIAL_TEXTURE_BINDING_MODE TextureBindingMode = MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC;

		// Fixed immutable sampler
		std::string LinearWrapSamplerName = "g_LinearWrapSampler";
		SamplerDesc LinearWrapSamplerDesc =
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
		};

		bool EqualsSampler(const MaterialCommonOptions& rhs) const
		{
			if (LinearWrapSamplerName != rhs.LinearWrapSamplerName)
			{
				return false;
			}

			return std::memcmp(&LinearWrapSamplerDesc, &rhs.LinearWrapSamplerDesc, sizeof(SamplerDesc)) == 0;
		}
	};

} // namespace shz
