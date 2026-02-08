#include "pch.h"
#include "Engine/RuntimeData/Public/MaterialTemplate.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace shz
{
	// ------------------------------------------------------------
	// Resource / value type conversions
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

		ASSERT(nextOffset > currOffset, "Next offset must be > current offset.");
		return nextOffset - currOffset;
	}

	static inline bool shaderHasResource(const IShader* pShader, const char* name, SHADER_RESOURCE_TYPE type)
	{
		ASSERT(pShader, "Shader is null.");
		ASSERT(name && name[0] != '\0', "Invalid name.");

		const uint32 resCount = pShader->GetResourceCount();
		for (uint32 r = 0; r < resCount; ++r)
		{
			ShaderResourceDesc rd = {};
			pShader->GetResourceDesc(r, rd);

			if (rd.Type != type)
				continue;

			if (rd.Name && std::strcmp(rd.Name, name) == 0)
				return true;
		}
		return false;
	}

	// ------------------------------------------------------------
	// Policy:
	// - If the shader declares a constant buffer named MATERIAL_CONSTANTS,
	//   we assume its reflection buffer desc is index 0.
	// - This matches your template policy: "Only one constant buffer is reflected".
	// - If you later support multiple CBs, you must extend reflection API.
	// ------------------------------------------------------------

	static inline const ShaderCodeBufferDesc* getMaterialCBufferDesc_PolicySingleCB(const IShader* pShader, uint32* pOutGlobalIndex)
	{
		ASSERT(pShader, "Shader is null.");
		ASSERT(pOutGlobalIndex, "pOutGlobalIndex is null.");
		*pOutGlobalIndex = 0;

		if (!shaderHasResource(pShader, MaterialTemplate::MATERIAL_CBUFFER_NAME, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER))
		{
			return nullptr;
		}

		// By policy, the only reflected constant buffer is MATERIAL_CONSTANTS and is index 0.
		const ShaderCodeBufferDesc* pCB = pShader->GetConstantBufferDesc(0);
		return pCB;
	}

	// ------------------------------------------------------------
	// MaterialTemplate
	// ------------------------------------------------------------

	bool MaterialTemplate::Initialize(IRenderDevice* pDevice, IShaderSourceInputStreamFactory* pShaderSourceFactory, const MaterialTemplateCreateInfo& ci)
	{
		ASSERT(pDevice, "Device is null.");
		ASSERT(pShaderSourceFactory, "Shader source factory is null.");

		m_CreateInfo = ci;
		m_PipelineType = ci.PipelineType;

		ASSERT(!ci.TemplateName.empty(), "Empty template name.");
		m_Name = ci.TemplateName;

		m_Shaders.clear();
		m_ValueParamLut.clear();
		m_ResourceLut.clear();
		m_CBuffers.clear();
		m_ValueParams.clear();
		m_Resources.clear();

		ASSERT(m_PipelineType != MATERIAL_PIPELINE_TYPE_UNKNOWN, "Invalid pipeline type.");
		ASSERT(!ci.ShaderStages.empty(), "No shader stages were provided.");

		// Build shaders
		{
			ASSERT(pDevice, "Device is null.");
			ASSERT(pShaderSourceFactory, "Shader source factory is null.");

			m_Shaders.clear();
			m_Shaders.reserve(ci.ShaderStages.size());

			ShaderCreateInfo sci = {};
			sci.pShaderSourceStreamFactory = pShaderSourceFactory;

			for (const MaterialShaderStageDesc& s : ci.ShaderStages)
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
				pDevice->CreateShader(sci, &pShader);

				if (!pShader)
				{
					ASSERT(false, "Failed to create shader: %s", s.FilePath.c_str());
					return false;
				}

				m_Shaders.push_back(pShader);
			}
		}

		// Build shader reflection
		{
			m_ValueParamLut.clear();
			m_ResourceLut.clear();
			m_CBuffers.clear();
			m_ValueParams.clear();
			m_Resources.clear();

			ASSERT(!m_Shaders.empty(), "No shaders in template.");

			bool bFoundMaterialCB = false;
			const uint32 matCbGlobalIndex = 0;

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
						ASSERT(var.Name && var.Name[0] != '\0', "Invalid variable name.");

						const uint32 absOffset = baseOffset + var.Offset;

						std::string fullName = prefix;
						if (!fullName.empty())
							fullName += ".";
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
						ASSERT(valueType != MATERIAL_VALUE_TYPE_UNKNOWN, "Unsupported variable type in MATERIAL_CONSTANTS.");

						uint32 leafSize = computeSiblingSize(pVars, varCount, i, parentEndOffset);
						if (leafSize == 0 && parentEndOffset > absOffset)
							leafSize = parentEndOffset - absOffset;

						ASSERT(leafSize > 0, "Invalid leaf size.");

						if (m_ValueParamLut.find(fullName) != m_ValueParamLut.end())
						{
							ASSERT(false, "Duplicate material value param name: %s", fullName.c_str());
							continue;
						}

						MaterialValueParamDesc P = {};
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

			for (const RefCntAutoPtr<IShader>& shaderRef : m_Shaders)
			{
				const IShader* pShader = shaderRef.RawPtr();
				ASSERT(pShader, "Shader is null.");

				// ------------------------------------------------------------
				// MATERIAL_CONSTANTS: policy single CB at reflection index 0
				// ------------------------------------------------------------
				{
					uint32 dummyGlobal = 0;
					const ShaderCodeBufferDesc* pCBDesc = getMaterialCBufferDesc_PolicySingleCB(pShader, &dummyGlobal);
					if (pCBDesc)
					{
						if (!bFoundMaterialCB)
						{
							bFoundMaterialCB = true;

							MaterialCBufferDesc CB = {};
							CB.Name = MATERIAL_CBUFFER_NAME;
							CB.ByteSize = 0;
							CB.IsDynamic = true;
							m_CBuffers.push_back(CB);
						}

						m_CBuffers[matCbGlobalIndex].ByteSize =
							std::max<uint32>(m_CBuffers[matCbGlobalIndex].ByteSize, pCBDesc->Size);

						if (pCBDesc->NumVariables > 0 && pCBDesc->pVariables)
						{
							flattenVars(
								pCBDesc->pVariables,
								pCBDesc->NumVariables,
								matCbGlobalIndex,
								0,
								pCBDesc->Size,
								"");
						}
					}
				}

				// ------------------------------------------------------------
				// Resources (SRV/UAV), dedup by name across stages
				// ------------------------------------------------------------
				const uint32 resCount = pShader->GetResourceCount();
				for (uint32 r = 0; r < resCount; ++r)
				{
					ShaderResourceDesc resDesc = {};
					pShader->GetResourceDesc(r, resDesc);

					if (!resDesc.Name || resDesc.Name[0] == '\0')
						continue;

					// Skip CB + samplers
					if (resDesc.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
						continue;

					if (resDesc.Type == SHADER_RESOURCE_TYPE_SAMPLER)
						continue;

					const std::string resourceName = resDesc.Name;
					if (m_ResourceLut.find(resourceName) != m_ResourceLut.end())
						continue;

					const MATERIAL_RESOURCE_TYPE matType = convertResourceType(resDesc);
					if (matType == MATERIAL_RESOURCE_TYPE_UNKNOWN)
					{
						// Ignore unsupported resource types safely.
						continue;
					}

					MaterialResourceDesc MR = {};
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
		ASSERT(pOutIndex, "pOutIndex is null.");
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
		{
			return nullptr;
		}

		return &m_Resources[it->second];
	}

	bool MaterialTemplate::FindResourceIndex(const char* name, uint32* pOutIndex) const
	{
		ASSERT(pOutIndex, "pOutIndex is null.");
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

	bool MaterialTemplate::Load(
		IRenderDevice* pDevice,
		IShaderSourceInputStreamFactory* pShaderSourceFactory,
		const char* templateName,
		std::string* outError)
	{
		if (outError) outError->clear();

		const std::string path = std::string("C:/Dev/ShizenEngine/Assets/Materials/Template/") + templateName + ".json";

		std::ifstream ifs(path);
		if (!ifs.is_open())
		{
			if (outError) *outError = "Failed to open file: " + path;
			ASSERT(false, "Failed to open file: %s", path.c_str());
			return false;
		}

		nlohmann::json j;
		try
		{
			ifs >> j;
		}
		catch (const std::exception& e)
		{
			if (outError) *outError = e.what();
			ASSERT(false, "Failed to parse JSON: %s", e.what());
			return false;
		}

		MaterialTemplateCreateInfo ci = {};
		ci.TemplateName = templateName;
		ci.PipelineType = (MATERIAL_PIPELINE_TYPE)j.value("pipeline_type", (int)MATERIAL_PIPELINE_TYPE_GRAPHICS);

		auto& stages = j["shader_stages"];
		if (!stages.is_array() || stages.empty())
		{
			if (outError) *outError = "shader_stages is empty.";
			ASSERT(false, "shader_stages is empty.");
			return false;
		}

		for (auto& s : stages)
		{
			MaterialShaderStageDesc sd = {};

			const std::string type = s.value("type", "");
			if (type == "VS") sd.ShaderType = SHADER_TYPE_VERTEX;
			else if (type == "PS") sd.ShaderType = SHADER_TYPE_PIXEL;
			else if (type == "CS") sd.ShaderType = SHADER_TYPE_COMPUTE;
			else
			{
				if (outError) *outError = "Unknown shader type: " + type;
				ASSERT(false, "Unknown shader type: %s", type.c_str());
				return false;
			}

			sd.DebugName = s.value("debug_name", "");
			sd.FilePath = s.value("file", "");
			sd.EntryPoint = s.value("entry", "main");
			sd.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sd.CompileFlags = (SHADER_COMPILE_FLAGS)s.value("compile_flags", (int)SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);

			sd.UseCombinedTextureSamplers = s.value("use_combined_texture_samplers", false);

			if (sd.FilePath.empty())
			{
				if (outError) *outError = "Shader file path is empty.";
				ASSERT(false, "Shader file path is empty.");
				return false;
			}

			ci.ShaderStages.push_back(sd);
		}

		return Initialize(pDevice, pShaderSourceFactory, m_CreateInfo);
	}

	bool MaterialTemplate::Save(std::string* outError) const
	{
		if (outError) outError->clear();

		ASSERT(!m_CreateInfo.TemplateName.empty(), "Template name is empty.");

		const std::string path =std::string("C:/Dev/ShizenEngine/Assets/Materials/Template/")+ m_CreateInfo.TemplateName + ".json";

		std::filesystem::create_directories(std::filesystem::path(path).parent_path());

		nlohmann::json j;
		j["version"] = 1;
		j["pipeline_type"] = (int)m_CreateInfo.PipelineType;

		nlohmann::json stages = nlohmann::json::array();
		for (const auto& s : m_CreateInfo.ShaderStages)
		{
			nlohmann::json sj;
			sj["type"] =
				(s.ShaderType == SHADER_TYPE_VERTEX) ? "VS" :
				(s.ShaderType == SHADER_TYPE_PIXEL) ? "PS" :
				(s.ShaderType == SHADER_TYPE_COMPUTE) ? "CS" : "UNKNOWN";

			sj["debug_name"] = s.DebugName;
			sj["file"] = s.FilePath;
			sj["entry"] = s.EntryPoint;
			sj["compile_flags"] = (int)s.CompileFlags;
			sj["use_combined_texture_samplers"] = s.UseCombinedTextureSamplers;

			stages.push_back(sj);
		}

		j["shader_stages"] = stages;

		std::ofstream ofs(path, std::ios::trunc);
		if (!ofs.is_open())
		{
			if (outError) *outError = "Failed to write file: " + path;
			ASSERT(false, "Failed to write file: %s", path.c_str());
			return false;
		}

		ofs << j.dump(4);
		return true;
	}
} // namespace shz
