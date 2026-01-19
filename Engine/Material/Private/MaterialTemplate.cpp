#include "pch.h"
#include "Engine/Material/Public/MaterialTemplate.h"

#include <cstring>
#include <algorithm>
#include <functional>

namespace shz
{
	static inline MATERIAL_RESOURCE_TYPE convertResourceType(const ShaderResourceDesc& resourceDesc)
	{
		switch (resourceDesc.Type)
		{
		case SHADER_RESOURCE_TYPE_TEXTURE_SRV:
		{
			ASSERT(resourceDesc.ArraySize > 0, "Array size must be greater than 0.");
			if (resourceDesc.ArraySize == 1)
			{
				return MATERIAL_RESOURCE_TYPE_TEXTURE2D;
			}
			else if (resourceDesc.ArraySize == 6)
			{
				return MATERIAL_RESOURCE_TYPE_TEXTURECUBE;
			}
			else
			{
				return MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY;
			}
		}
		case SHADER_RESOURCE_TYPE_BUFFER_SRV:
		{
			return MATERIAL_RESOURCE_TYPE_STRUCTUREDBUFFER;
		}
		case SHADER_RESOURCE_TYPE_BUFFER_UAV:
		{
			return MATERIAL_RESOURCE_TYPE_RWSTRUCTUREDBUFFER;
		}
		default:
		{
			return MATERIAL_RESOURCE_TYPE_UNKNOWN;
		}
		}
	}

	static inline MATERIAL_VALUE_TYPE convertValueType(const ShaderCodeVariableDesc& var)
	{
		auto isScalarOrVector = [](SHADER_CODE_VARIABLE_CLASS c) -> bool
		{
			return c == SHADER_CODE_VARIABLE_CLASS_SCALAR || c == SHADER_CODE_VARIABLE_CLASS_VECTOR;
		};

		auto isMatrix = [](SHADER_CODE_VARIABLE_CLASS c) -> bool
		{
			return c == SHADER_CODE_VARIABLE_CLASS_MATRIX_ROWS || c == SHADER_CODE_VARIABLE_CLASS_MATRIX_COLUMNS;
		};

		if (var.Class == SHADER_CODE_VARIABLE_CLASS_STRUCT)
		{
			return MATERIAL_VALUE_TYPE_UNKNOWN;
		}

		if (isMatrix(var.Class))
		{
			if (var.BasicType == SHADER_CODE_BASIC_TYPE_FLOAT && var.NumRows == 4 && var.NumColumns == 4)
			{
				return MATERIAL_VALUE_TYPE_FLOAT4X4;
			}
			return MATERIAL_VALUE_TYPE_UNKNOWN;
		}

		if (!isScalarOrVector(var.Class))
		{
			return MATERIAL_VALUE_TYPE_UNKNOWN;
		}

		if (var.BasicType == SHADER_CODE_BASIC_TYPE_FLOAT)
		{
			if (var.NumColumns == 1) return MATERIAL_VALUE_TYPE_FLOAT;
			if (var.NumColumns == 2) return MATERIAL_VALUE_TYPE_FLOAT2;
			if (var.NumColumns == 3) return MATERIAL_VALUE_TYPE_FLOAT3;
			if (var.NumColumns == 4) return MATERIAL_VALUE_TYPE_FLOAT4;
		}
		else if (var.BasicType == SHADER_CODE_BASIC_TYPE_INT)
		{
			if (var.NumColumns == 1) return MATERIAL_VALUE_TYPE_INT;
			if (var.NumColumns == 2) return MATERIAL_VALUE_TYPE_INT2;
			if (var.NumColumns == 3) return MATERIAL_VALUE_TYPE_INT3;
			if (var.NumColumns == 4) return MATERIAL_VALUE_TYPE_INT4;
		}
		else if (var.BasicType == SHADER_CODE_BASIC_TYPE_UINT)
		{
			if (var.NumColumns == 1) return MATERIAL_VALUE_TYPE_UINT;
			if (var.NumColumns == 2) return MATERIAL_VALUE_TYPE_UINT2;
			if (var.NumColumns == 3) return MATERIAL_VALUE_TYPE_UINT3;
			if (var.NumColumns == 4) return MATERIAL_VALUE_TYPE_UINT4;
		}

		return MATERIAL_VALUE_TYPE_UNKNOWN;
	}

	static inline uint32 computeSiblingSize(
		const ShaderCodeVariableDesc* pVars,
		uint32 varCount,
		uint32 varIndex,
		uint32 parentEndOffset)
	{
		const uint32 currOffset = pVars[varIndex].Offset;
		uint32 nextOffset = parentEndOffset;

		for (uint32 i = varIndex + 1; i < varCount; ++i)
		{
			const uint32 off = pVars[i].Offset;
			if (off > currOffset)
			{
				nextOffset = off;
				break;
			}
		}

		ASSERT(nextOffset > currOffset, "Next offset must be bigger than current offset.");

		return nextOffset - currOffset;
	}

	bool MaterialTemplate::BuildFromShaders(const std::vector<const IShader*>& pShaders)
	{
		m_ValueParamLut.clear();
		m_ResourceLut.clear();
		m_CBuffers.clear();
		m_ValueParams.clear();
		m_Resources.clear();

		m_pShaders = pShaders;
		uint32 numShaders = static_cast<uint>(m_pShaders.size());

		ASSERT(numShaders > 0, "At least one shader is needed.");

		// We keep only MATERIAL_CONSTANTS.
		bool bFoundMaterialCB = false;
		uint32 MaterialCB_GlobalIndex = 0;

		// We still need a stable "localIndex" for GetConstantBufferDesc(localIndex).
		// We'll build local mapping per shader.
		std::function<void(const ShaderCodeVariableDesc*, uint32, uint32, uint32, uint32, const std::string&)> flattenVars;
		flattenVars = [&](
			const ShaderCodeVariableDesc* pVars,
			uint32 varCount,
			uint32 globalCBufferIndex,
			uint32 baseOffset,
			uint32 parentEndOffset,
			const std::string& prefix)
		{
			ASSERT(pVars && varCount > 0, "Invalid arguments.");

			for (uint32 i = 0; i < varCount; ++i)
			{
				const ShaderCodeVariableDesc& var = pVars[i];
				ASSERT(var.Name && var.Name[0] != '\0', "Invalid variable name");

				const uint32 absOffset = baseOffset + var.Offset;

				std::string fullName = prefix;
				if (!fullName.empty()) fullName += ".";
				fullName += var.Name;

				if (var.Class == SHADER_CODE_VARIABLE_CLASS_STRUCT && var.NumMembers > 0 && var.pMembers)
				{
					const uint32 structSize = computeSiblingSize(pVars, varCount, i, parentEndOffset);
					const uint32 structEnd = (structSize != 0) ? (absOffset + structSize) : parentEndOffset;

					flattenVars(
						var.pMembers,
						var.NumMembers,
						globalCBufferIndex,
						absOffset,
						structEnd,
						fullName);
					continue;
				}

				const MATERIAL_VALUE_TYPE valueType = convertValueType(var);
				ASSERT(valueType != MATERIAL_VALUE_TYPE_UNKNOWN, "Type is unkown.");

				uint32 leafSize = computeSiblingSize(pVars, varCount, i, parentEndOffset);
				if (leafSize == 0 && parentEndOffset > absOffset)
				{
					leafSize = parentEndOffset - absOffset;
				}
				ASSERT(leafSize > 0, "At least one leaf needed.");

				// Dedup by name across stages
				if (m_ValueParamLut.find(fullName) != m_ValueParamLut.end())
				{
					ASSERT(false, "%s is not a valid variable name of shader.", fullName);
					continue;
				}

				MaterialValueParamDesc P = {};
				P.Name = fullName;
				P.Type = valueType;
				P.CBufferIndex = static_cast<uint16>(globalCBufferIndex);
				P.ByteOffset = static_cast<uint16>(absOffset);
				P.ByteSize = static_cast<uint16>(std::min<uint32>(leafSize, 0xFFFFu));
				P.Flags = MaterialParamFlags_None;

				const uint32 newIndex = static_cast<uint32>(m_ValueParams.size());
				m_ValueParams.push_back(P);
				m_ValueParamLut.emplace(fullName, newIndex);
			}
		};

		for (uint32 s = 0; s < numShaders; ++s)
		{
			const IShader* pShader = m_pShaders[s];
			ASSERT(pShader, "Shader is null.");

			std::unordered_map<std::string, uint32> localCbNameToIndex = {};
			localCbNameToIndex.reserve(16);
			uint32 localCbCounter = 0;

			const uint32 resCount = pShader->GetResourceCount();
			for (uint32 r = 0; r < resCount; ++r)
			{
				ShaderResourceDesc resourceDesc = {};
				pShader->GetResourceDesc(r, resourceDesc);
				ASSERT(resourceDesc.Name && resourceDesc.Name[0] != '\0', "Invalid resource name.");

				// Constant buffer: only MATERIAL_CONSTANTS
				if (resourceDesc.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
				{
					const std::string cbName = resourceDesc.Name;
					if (cbName != MATERIAL_CBUFFER_NAME)
					{
						// Ignore FRAME/OBJECT/SHADOW/whatever. Renderer owns them.
						continue;
					}

					uint32 localIndex = 0;
					auto itLocal = localCbNameToIndex.find(cbName);
					if (itLocal == localCbNameToIndex.end())
					{
						localIndex = localCbCounter++;
						localCbNameToIndex.emplace(cbName, localIndex);
					}
					else
					{
						localIndex = itLocal->second;
					}

					if (!bFoundMaterialCB)
					{
						bFoundMaterialCB = true;
						MaterialCB_GlobalIndex = 0;

						MaterialCBufferDesc CB = {};
						CB.Name = cbName;
						CB.ByteSize = 0;
						CB.IsDynamic = true;
						m_CBuffers.push_back(CB);
					}

					const ShaderCodeBufferDesc* pCBDesc = pShader->GetConstantBufferDesc(localIndex);
					ASSERT(pCBDesc, "Constant buffer desc of index %d is null.", localIndex);

					m_CBuffers[MaterialCB_GlobalIndex].ByteSize =
						std::max<uint32>(m_CBuffers[MaterialCB_GlobalIndex].ByteSize, pCBDesc->Size);

					// Prefix:
					// - We intentionally omit CB name prefix for convenience: "BaseColor" not "MATERIAL_CONSTANTS.BaseColor"
					// - Still supports structs as "MyStruct.Member"
					flattenVars(
						pCBDesc->pVariables,
						pCBDesc->NumVariables,
						MaterialCB_GlobalIndex,
						0,
						pCBDesc->Size,
						"");

					continue;
				}

				// Skip explicit sampler resources (we'll use immutable samplers or engine-side samplers)
				if (resourceDesc.Type == SHADER_RESOURCE_TYPE_SAMPLER)
				{
					continue;
				}

				// Other resources
				{
					const std::string resourceName = resourceDesc.Name;

					if (m_ResourceLut.find(resourceName) != m_ResourceLut.end())
					{
						continue;
					}

					const MATERIAL_RESOURCE_TYPE matType = convertResourceType(resourceDesc);
					ASSERT(matType != MATERIAL_RESOURCE_TYPE_UNKNOWN, "Material type is unkown.");

					MaterialResourceDesc resourceDesc = {};
					resourceDesc.Name = resourceName;
					resourceDesc.Type = matType;
					resourceDesc.ArraySize = static_cast<uint16>(std::max<uint32>(resourceDesc.ArraySize, 1u));
					resourceDesc.IsDynamic = true;

					const uint32 newIndex = static_cast<uint32>(m_Resources.size());
					m_Resources.push_back(resourceDesc);
					m_ResourceLut.emplace(resourceName, newIndex);
				}
			}
		}

		return true;
	}

	const MaterialValueParamDesc* MaterialTemplate::FindValueParam(const char* name) const
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");

		auto it = m_ValueParamLut.find(name);
		if (it == m_ValueParamLut.end())
		{
			return nullptr;
		}

		return &m_ValueParams[it->second];
	}

	bool MaterialTemplate::FindValueParamIndex(const char* name, uint32* pOutIndex) const
	{
		*pOutIndex = 0;
		ASSERT(name && name[0] != '\0', "Invalid name.");

		auto it = m_ValueParamLut.find(name);
		if (it == m_ValueParamLut.end())
		{
			return false;
		}

		*pOutIndex = it->second;
		return true;
	}

	const MaterialResourceDesc* MaterialTemplate::FindResource(const char* name) const
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");

		auto it = m_ResourceLut.find(name);
		if (it == m_ResourceLut.end())
			return nullptr;

		return &m_Resources[it->second];
	}

	bool MaterialTemplate::FindResourceIndex(const char* name, uint32* pOutIndex) const
	{
		*pOutIndex = 0;
		ASSERT(name && name[0] != '\0', "Invalid name.");

		auto it = m_ResourceLut.find(name);
		if (it == m_ResourceLut.end())
		{
			return false;
		}

		*pOutIndex = it->second;
		return true;
	}

	bool MaterialTemplate::ValidateSetValue(const char* name, MATERIAL_VALUE_TYPE expectedType, MaterialValueParamDesc* pOutDesc) const
	{
		const MaterialValueParamDesc* pDesc = FindValueParam(name);
		if (!pDesc)
		{
			return false;
		}

		if (expectedType != MATERIAL_VALUE_TYPE_UNKNOWN && pDesc->Type != expectedType)
		{
			return false;
		}

		if (pOutDesc)
		{
			*pOutDesc = *pDesc;
		}

		return true;
	}

	bool MaterialTemplate::ValidateSetResource(const char* name, MATERIAL_RESOURCE_TYPE expectedType, MaterialResourceDesc* pOutDesc) const
	{
		const MaterialResourceDesc* pDesc = FindResource(name);
		if (!pDesc)
		{
			return false;
		}

		if (expectedType != MATERIAL_RESOURCE_TYPE_UNKNOWN && pDesc->Type != expectedType)
		{
			return false;
		}

		if (pOutDesc)
		{
			*pOutDesc = *pDesc;
		}

		return true;
	}

} // namespace shz
