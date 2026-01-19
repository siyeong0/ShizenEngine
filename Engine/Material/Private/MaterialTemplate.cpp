#include "pch.h"
#include "Engine/Material/Public/MaterialTemplate.h"

#include <cstring>
#include <algorithm>
#include <functional>

namespace shz
{
	static inline MATERIAL_RESOURCE_TYPE convertResourceType(const ShaderResourceDesc& Res)
	{
		switch (Res.Type)
		{
		case SHADER_RESOURCE_TYPE_TEXTURE_SRV:
			return MATERIAL_RESOURCE_TYPE_TEXTURE2D; // (선택) dimension 있으면 2D/Array/Cube 구분
		case SHADER_RESOURCE_TYPE_BUFFER_SRV:
			return MATERIAL_RESOURCE_TYPE_STRUCTUREDBUFFER;
		case SHADER_RESOURCE_TYPE_BUFFER_UAV:
			return MATERIAL_RESOURCE_TYPE_RWSTRUCTUREDBUFFER;
		default:
			return MATERIAL_RESOURCE_TYPE_UNKNOWN;
		}
	}

	static inline MATERIAL_VALUE_TYPE convertValueType(const ShaderCodeVariableDesc& Var)
	{
		auto IsScalarOrVector = [](SHADER_CODE_VARIABLE_CLASS c) -> bool
		{
			return c == SHADER_CODE_VARIABLE_CLASS_SCALAR ||
				c == SHADER_CODE_VARIABLE_CLASS_VECTOR;
		};

		auto IsMatrix = [](SHADER_CODE_VARIABLE_CLASS c) -> bool
		{
			return c == SHADER_CODE_VARIABLE_CLASS_MATRIX_ROWS ||
				c == SHADER_CODE_VARIABLE_CLASS_MATRIX_COLUMNS;
		};

		if (Var.Class == SHADER_CODE_VARIABLE_CLASS_STRUCT)
			return MATERIAL_VALUE_TYPE_UNKNOWN;

		if (IsMatrix(Var.Class))
		{
			if (Var.BasicType == SHADER_CODE_BASIC_TYPE_FLOAT && Var.NumRows == 4 && Var.NumColumns == 4)
				return MATERIAL_VALUE_TYPE_FLOAT4X4;
			return MATERIAL_VALUE_TYPE_UNKNOWN;
		}

		if (!IsScalarOrVector(Var.Class))
			return MATERIAL_VALUE_TYPE_UNKNOWN;

		if (Var.BasicType == SHADER_CODE_BASIC_TYPE_FLOAT)
		{
			if (Var.NumColumns == 1) return MATERIAL_VALUE_TYPE_FLOAT;
			if (Var.NumColumns == 2) return MATERIAL_VALUE_TYPE_FLOAT2;
			if (Var.NumColumns == 3) return MATERIAL_VALUE_TYPE_FLOAT3;
			if (Var.NumColumns == 4) return MATERIAL_VALUE_TYPE_FLOAT4;
		}
		else if (Var.BasicType == SHADER_CODE_BASIC_TYPE_INT)
		{
			if (Var.NumColumns == 1) return MATERIAL_VALUE_TYPE_INT;
			if (Var.NumColumns == 2) return MATERIAL_VALUE_TYPE_INT2;
			if (Var.NumColumns == 3) return MATERIAL_VALUE_TYPE_INT3;
			if (Var.NumColumns == 4) return MATERIAL_VALUE_TYPE_INT4;
		}
		else if (Var.BasicType == SHADER_CODE_BASIC_TYPE_UINT)
		{
			if (Var.NumColumns == 1) return MATERIAL_VALUE_TYPE_UINT;
			if (Var.NumColumns == 2) return MATERIAL_VALUE_TYPE_UINT2;
			if (Var.NumColumns == 3) return MATERIAL_VALUE_TYPE_UINT3;
			if (Var.NumColumns == 4) return MATERIAL_VALUE_TYPE_UINT4;
		}

		return MATERIAL_VALUE_TYPE_UNKNOWN;
	}

	static inline uint32 computeSiblingSize(
		const ShaderCodeVariableDesc* pVars,
		uint32 varCount,
		uint32 varIndex,
		uint32 parentEndOffset)
	{
		const uint32 curOffset = pVars[varIndex].Offset;
		uint32 nextOffset = parentEndOffset;

		for (uint32 i = varIndex + 1; i < varCount; ++i)
		{
			const uint32 off = pVars[i].Offset;
			if (off > curOffset)
			{
				nextOffset = off;
				break;
			}
		}

		if (nextOffset <= curOffset)
			return 0;

		return nextOffset - curOffset;
	}

	bool MaterialTemplate::BuildFromShaders(const IShader* const* ppShaders, uint32 shaderCount)
	{
		m_ValueParamLut.clear();
		m_ResourceLut.clear();
		m_CBuffers.clear();
		m_ValueParams.clear();
		m_Resources.clear();

		if (!ppShaders || shaderCount == 0)
			return false;

		// We keep only MATERIAL_CONSTANTS.
		bool FoundMaterialCB = false;
		uint32 MaterialCB_GlobalIndex = 0;

		// We still need a stable "localIndex" for GetConstantBufferDesc(localIndex).
		// We'll build local mapping per shader.
		std::function<void(const ShaderCodeVariableDesc*, uint32, uint32, uint32, uint32, const std::string&)> FlattenVars;
		FlattenVars = [&](const ShaderCodeVariableDesc* pVars,
			uint32 varCount,
			uint32 globalCBufferIndex,
			uint32 baseOffset,
			uint32 parentEndOffset,
			const std::string& prefix)
		{
			if (!pVars || varCount == 0)
				return;

			for (uint32 i = 0; i < varCount; ++i)
			{
				const ShaderCodeVariableDesc& Var = pVars[i];
				if (!Var.Name || Var.Name[0] == '\0')
					continue;

				const uint32 absOffset = baseOffset + Var.Offset;

				std::string fullName = prefix;
				if (!fullName.empty())
					fullName += ".";
				fullName += Var.Name;

				if (Var.Class == SHADER_CODE_VARIABLE_CLASS_STRUCT && Var.NumMembers > 0 && Var.pMembers)
				{
					const uint32 structSize = computeSiblingSize(pVars, varCount, i, parentEndOffset);
					const uint32 structEnd = (structSize != 0) ? (absOffset + structSize) : parentEndOffset;

					FlattenVars(
						Var.pMembers,
						Var.NumMembers,
						globalCBufferIndex,
						absOffset,
						structEnd,
						fullName);
					continue;
				}

				const MATERIAL_VALUE_TYPE valueType = convertValueType(Var);
				if (valueType == MATERIAL_VALUE_TYPE_UNKNOWN)
					continue;

				uint32 leafSize = computeSiblingSize(pVars, varCount, i, parentEndOffset);
				if (leafSize == 0 && parentEndOffset > absOffset)
					leafSize = parentEndOffset - absOffset;

				if (leafSize == 0)
					continue;

				// Dedup by name across stages
				if (m_ValueParamLut.find(fullName) != m_ValueParamLut.end())
					continue;

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

		for (uint32 s = 0; s < shaderCount; ++s)
		{
			const IShader* pShader = ppShaders[s];
			if (!pShader)
				continue;

			std::unordered_map<std::string, uint32> localCbNameToIndex = {};
			localCbNameToIndex.reserve(16);
			uint32 localCbCounter = 0;

			const uint32 resCount = pShader->GetResourceCount();
			for (uint32 r = 0; r < resCount; ++r)
			{
				ShaderResourceDesc Res = {};
				pShader->GetResourceDesc(r, Res);

				if (!Res.Name || Res.Name[0] == '\0')
					continue;

				// Constant buffer: only MATERIAL_CONSTANTS
				if (Res.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
				{
					const std::string cbName = Res.Name;
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

					if (!FoundMaterialCB)
					{
						FoundMaterialCB = true;
						MaterialCB_GlobalIndex = 0;

						MaterialCBufferDesc CB = {};
						CB.Name = cbName;
						CB.ByteSize = 0;
						CB.IsDynamic = true;
						m_CBuffers.push_back(CB);
					}

					const ShaderCodeBufferDesc* pCBDesc = pShader->GetConstantBufferDesc(localIndex);
					if (!pCBDesc)
						continue;

					m_CBuffers[MaterialCB_GlobalIndex].ByteSize =
						std::max<uint32>(m_CBuffers[MaterialCB_GlobalIndex].ByteSize, pCBDesc->Size);

					// Prefix:
					// - We intentionally omit CB name prefix for convenience: "BaseColor" not "MATERIAL_CONSTANTS.BaseColor"
					// - Still supports structs as "MyStruct.Member"
					FlattenVars(
						pCBDesc->pVariables,
						pCBDesc->NumVariables,
						MaterialCB_GlobalIndex,
						0,
						pCBDesc->Size,
						"");

					continue;
				}

				// Skip explicit sampler resources (we'll use immutable samplers or engine-side samplers)
				if (Res.Type == SHADER_RESOURCE_TYPE_SAMPLER)
					continue;

				// Other resources
				{
					const std::string resName = Res.Name;

					if (m_ResourceLut.find(resName) != m_ResourceLut.end())
						continue;

					const MATERIAL_RESOURCE_TYPE matType = convertResourceType(Res);
					if (matType == MATERIAL_RESOURCE_TYPE_UNKNOWN)
						continue;

					MaterialResourceDesc RD = {};
					RD.Name = resName;
					RD.Type = matType;
					RD.ArraySize = static_cast<uint16>(std::max<uint32>(Res.ArraySize, 1u));
					RD.IsDynamic = true;

					const uint32 newIndex = static_cast<uint32>(m_Resources.size());
					m_Resources.push_back(RD);
					m_ResourceLut.emplace(resName, newIndex);
				}
			}
		}

		// MaterialConstants가 없으면, template이 “텍스처만 있는 머터리얼”일 수도 있으니 허용.
		// 하지만 instance가 cbuffer를 기대할 수 있으므로, 0개도 정상으로 둔다.
		return true;
	}

	const MaterialValueParamDesc* MaterialTemplate::FindValueParam(const char* name) const
	{
		if (!name || name[0] == '\0')
			return nullptr;

		auto it = m_ValueParamLut.find(name);
		if (it == m_ValueParamLut.end())
			return nullptr;

		return &m_ValueParams[it->second];
	}

	bool MaterialTemplate::FindValueParamIndex(const char* name, uint32& outIndex) const
	{
		outIndex = 0;
		if (!name || name[0] == '\0')
			return false;

		auto it = m_ValueParamLut.find(name);
		if (it == m_ValueParamLut.end())
			return false;

		outIndex = it->second;
		return true;
	}

	const MaterialResourceDesc* MaterialTemplate::FindResource(const char* name) const
	{
		if (!name || name[0] == '\0')
			return nullptr;

		auto it = m_ResourceLut.find(name);
		if (it == m_ResourceLut.end())
			return nullptr;

		return &m_Resources[it->second];
	}

	bool MaterialTemplate::FindResourceIndex(const char* name, uint32& outIndex) const
	{
		outIndex = 0;
		if (!name || name[0] == '\0')
			return false;

		auto it = m_ResourceLut.find(name);
		if (it == m_ResourceLut.end())
			return false;

		outIndex = it->second;
		return true;
	}

	bool MaterialTemplate::ValidateSetValue(const char* name, MATERIAL_VALUE_TYPE expectedType, MaterialValueParamDesc* pOutDesc) const
	{
		const MaterialValueParamDesc* pDesc = FindValueParam(name);
		if (!pDesc)
			return false;

		if (expectedType != MATERIAL_VALUE_TYPE_UNKNOWN && pDesc->Type != expectedType)
			return false;

		if (pOutDesc)
			*pOutDesc = *pDesc;

		return true;
	}

	bool MaterialTemplate::ValidateSetResource(const char* name, MATERIAL_RESOURCE_TYPE expectedType, MaterialResourceDesc* pOutDesc) const
	{
		const MaterialResourceDesc* pDesc = FindResource(name);
		if (!pDesc)
			return false;

		if (expectedType != MATERIAL_RESOURCE_TYPE_UNKNOWN && pDesc->Type != expectedType)
			return false;

		if (pOutDesc)
			*pOutDesc = *pDesc;

		return true;
	}

} // namespace shz
