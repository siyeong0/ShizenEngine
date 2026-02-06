#include "pch.h"
#include "Engine/Renderer/Public/Shader.h"

#include "Engine/Renderer/Public/Renderer.h"

#include <algorithm>
#include <functional>
#include <cstring>

namespace shz
{
	// ------------------------------------------------------------
	// Helpers: type conversions
	// ------------------------------------------------------------

	static inline MATERIAL_RESOURCE_TYPE convertResourceType(const ShaderResourceDesc& resourceDesc)
	{
		switch (resourceDesc.Type)
		{
		case SHADER_RESOURCE_TYPE_TEXTURE_SRV:
		{
			ASSERT(resourceDesc.ArraySize > 0, "Array size must be > 0.");
			if (resourceDesc.ArraySize == 1) return MATERIAL_RESOURCE_TYPE_TEXTURE2D;
			if (resourceDesc.ArraySize == 6) return MATERIAL_RESOURCE_TYPE_TEXTURECUBE;
			return MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY;
		}
		case SHADER_RESOURCE_TYPE_BUFFER_SRV:
			return MATERIAL_RESOURCE_TYPE_STRUCTUREDBUFFER;

		case SHADER_RESOURCE_TYPE_BUFFER_UAV:
			return MATERIAL_RESOURCE_TYPE_RWSTRUCTUREDBUFFER;

		default:
			return MATERIAL_RESOURCE_TYPE_UNKNOWN;
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
			return MATERIAL_VALUE_TYPE_UNKNOWN;

		if (isMatrix(var.Class))
		{
			if (var.BasicType == SHADER_CODE_BASIC_TYPE_FLOAT && var.NumRows == 4 && var.NumColumns == 4)
				return MATERIAL_VALUE_TYPE_FLOAT4X4;

			return MATERIAL_VALUE_TYPE_UNKNOWN;
		}

		if (!isScalarOrVector(var.Class))
			return MATERIAL_VALUE_TYPE_UNKNOWN;

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

		ASSERT(nextOffset > currOffset, "Next offset must be > current offset.");
		return nextOffset - currOffset;
	}

	// ------------------------------------------------------------
	// MaterialTemplate
	// ------------------------------------------------------------
	Shader::Shader(const std::vector<StageDesc>& stages)
	{
		ASSERT(s_pRenderer, "Renderer is not registered. Call RegisterRenderer() first.");
		ASSERT(!stages.empty(), "Shader stages are empty.");

		// ------------------------------------------------------------
		// 1) Create shaders
		// ------------------------------------------------------------
		{
			ShaderCreateInfo sci = {};
			for (const StageDesc& s : stages)
			{
				ASSERT(s.ShaderType != SHADER_TYPE_UNKNOWN, "Invalid shader stage type.");
				ASSERT(!s.FilePath.empty(), "Shader file path is empty.");

				sci.SourceLanguage = s.SourceLanguage;
				sci.EntryPoint = s.EntryPoint.c_str();
				sci.CompileFlags = s.CompileFlags;
				sci.LoadConstantBufferReflection = true;

				sci.Desc = {};
				sci.Desc.Name = s.DebugName.empty() ? "Material Shader" : s.DebugName.c_str();
				sci.Desc.ShaderType = s.ShaderType;
				sci.Desc.UseCombinedTextureSamplers = s.UseCombinedTextureSamplers;
				sci.FilePath = s.FilePath.c_str();

				RefCntAutoPtr<IShader> pShader;
				s_pRenderer->CreateShader(sci, &pShader);
				ASSERT(pShader, "Failed to create shader: %s", s.FilePath.c_str());

				ASSERT(m_Shaders.find(s.ShaderType) == m_Shaders.end(), "Duplicate shader stage in template: %u", (uint32)s.ShaderType);
				m_Shaders.emplace(s.ShaderType, pShader);
			}

			ASSERT(!m_Shaders.empty(), "No shaders were created.");
		}

		// ------------------------------------------------------------
		// 2) Reflection: scan resource table
		// ------------------------------------------------------------
		{
			// CBName -> global index in m_CBuffers
			std::unordered_map<std::string, uint32> cbufferLut;

			// Flatten helper (struct recursion)
			std::function<void(
				const ShaderCodeVariableDesc*,
				uint32,
				uint32, // globalCBufferIndex
				uint32, // baseOffset
				uint32, // parentEndOffset
				const std::string& // prefix
				)> flattenVars;

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
					ASSERT(var.Name && var.Name[0] != '\0', "Invalid variable name.");

					const uint32 absOffset = baseOffset + var.Offset;

					std::string fullName = prefix;
					if (!fullName.empty())
						fullName += ".";
					fullName += var.Name;

					// struct recurse
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
					ASSERT(valueType != MATERIAL_VALUE_TYPE_UNKNOWN, "Unsupported variable type in constant buffer.");

					uint32 leafSize = computeSiblingSize(pVars, varCount, i, parentEndOffset);
					if (leafSize == 0 && parentEndOffset > absOffset)
						leafSize = parentEndOffset - absOffset;

					ASSERT(leafSize > 0, "Invalid leaf size.");

					if (m_ValueParamLut.find(fullName) != m_ValueParamLut.end())
					{
						ASSERT(false, "Duplicate material value param name: %s", fullName.c_str());
						continue;
					}

					ValueParamDesc P = {};
					P.Name = fullName;
					P.Type = valueType;
					P.CBufferIndex = globalCBufferIndex;
					P.ByteOffset = absOffset;
					P.ByteSize = leafSize;
					P.Flags = MaterialParamFlags_None;

					const uint32 newIndex = static_cast<uint32>(m_ValueParams.size());
					m_ValueParams.push_back(P);
					m_ValueParamLut.emplace(fullName, newIndex);
				}
			};

			// Iterate all shaders
			for (const auto& kv : m_Shaders)
			{
				const IShader* pShader = kv.second.RawPtr();
				ASSERT(pShader, "Shader is null.");

				const uint32 resCount = pShader->GetResourceCount();
				for (uint32 r = 0; r < resCount; ++r)
				{
					ShaderResourceDesc resDesc = {};
					pShader->GetResourceDesc(r, resDesc);

					// skip sampler
					if (resDesc.Type == SHADER_RESOURCE_TYPE_SAMPLER)
					{
						continue;
					}

					// ------------------------------------------------------------
					// Constant Buffer
					// ------------------------------------------------------------
					if (resDesc.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
					{
						const char* cbName = resDesc.Name;

						if (s_pRenderer->IsCommonStaticResource(cbName))
						{
							continue;
						}

						// find/add cbuffer
						uint32 globalIndex = 0;
						{
							auto it = cbufferLut.find(cbName);
							if (it == cbufferLut.end())
							{
								CBufferDesc CB = {};
								CB.Name = cbName;
								CB.ByteSize = 0;
								CB.IsDynamic = true;

								globalIndex = static_cast<uint32>(m_CBuffers.size());
								m_CBuffers.push_back(CB);
								cbufferLut.emplace(cbName, globalIndex);
							}
							else
							{
								globalIndex = it->second;
							}
						}

						// get reflection desc
						const ShaderCodeBufferDesc* pCBDesc = pShader->GetConstantBufferDesc(r);
						ASSERT(pCBDesc, "Constant buffer reflection desc is null. cb=%s (resourceIndex=%u)", cbName, r);

						// accumulate size across stages
						m_CBuffers[globalIndex].ByteSize =
							std::max<uint32>(m_CBuffers[globalIndex].ByteSize, pCBDesc->Size);

						// flatten variables
						if (pCBDesc->NumVariables > 0 && pCBDesc->pVariables)
						{
							flattenVars(
								pCBDesc->pVariables,
								pCBDesc->NumVariables,
								globalIndex,
								0,
								pCBDesc->Size,
								"");
						}

						continue;
					}

					// ------------------------------------------------------------
					// Other Resources (SRV/UAV)
					// ------------------------------------------------------------
					{
						const std::string resourceName = resDesc.Name;
						if (m_ResourceLut.find(resourceName) != m_ResourceLut.end())
							continue;

						const MATERIAL_RESOURCE_TYPE matType = convertResourceType(resDesc);
						if (matType == MATERIAL_RESOURCE_TYPE_UNKNOWN)
							continue;

						ResourceDesc MR = {};
						MR.Name = resourceName;
						MR.Type = matType;
						MR.ArraySize = static_cast<uint16>(std::max<uint32>(resDesc.ArraySize, 1u));
						MR.IsDynamic = true;

						const uint32 newIndex = static_cast<uint32>(m_Resources.size());
						m_Resources.push_back(MR);
						m_ResourceLut.emplace(resourceName, newIndex);
					}
				}
			}
		}
	}

	const Shader::ValueParamDesc* Shader::FindValueParam(const std::string& name) const
	{
		if (name.empty())
			return nullptr;

		auto it = m_ValueParamLut.find(name);
		if (it == m_ValueParamLut.end())
			return nullptr;

		return &m_ValueParams[it->second];
	}

	bool Shader::FindValueParamIndex(const char* name, uint32* pOutIndex) const
	{
		ASSERT(pOutIndex, "pOutIndex is null.");
		*pOutIndex = 0;

		ASSERT(name && name[0] != '\0', "Invalid name.");

		auto it = m_ValueParamLut.find(name);
		if (it == m_ValueParamLut.end())
			return false;

		*pOutIndex = it->second;
		return true;
	}

	const Shader::ResourceDesc* Shader::FindResource(const char* name) const
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");

		auto it = m_ResourceLut.find(name);
		if (it == m_ResourceLut.end())
			return nullptr;

		return &m_Resources[it->second];
	}

	bool Shader::FindResourceIndex(const char* name, uint32* pOutIndex) const
	{
		ASSERT(pOutIndex, "pOutIndex is null.");
		*pOutIndex = 0;

		ASSERT(name && name[0] != '\0', "Invalid name.");

		auto it = m_ResourceLut.find(name);
		if (it == m_ResourceLut.end())
			return false;

		*pOutIndex = it->second;
		return true;
	}

} // namespace shz
