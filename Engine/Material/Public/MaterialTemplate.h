#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IRenderDevice.h"

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

	enum MATERIAL_PIPELINE_TYPE : uint8
	{
		MATERIAL_PIPELINE_TYPE_UNKNOWN = 0,
		MATERIAL_PIPELINE_TYPE_GRAPHICS,
		MATERIAL_PIPELINE_TYPE_COMPUTE,
	};

	struct MaterialShaderStageDesc final
	{
		SHADER_TYPE ShaderType = SHADER_TYPE_UNKNOWN;

		std::string DebugName = {};
		std::string FilePath = {};
		std::string EntryPoint = "main";

		SHADER_SOURCE_LANGUAGE SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		SHADER_COMPILE_FLAGS   CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		bool UseCombinedTextureSamplers = false;
	};

	struct MaterialTemplateCreateInfo final
	{
		MATERIAL_PIPELINE_TYPE PipelineType = MATERIAL_PIPELINE_TYPE_UNKNOWN;

		std::string TemplateName = {};
		std::vector<MaterialShaderStageDesc> ShaderStages = {};
	};

	struct MaterialValueParamDesc final
	{
		std::string Name = {};
		MATERIAL_VALUE_TYPE Type = MATERIAL_VALUE_TYPE_UNKNOWN;

		uint32 CBufferIndex = 0;
		uint32 ByteOffset = 0;
		uint32 ByteSize = 0;

		MaterialParamFlags Flags = MaterialParamFlags_None;
	};

	struct MaterialCBufferDesc final
	{
		std::string Name = {};
		uint32 ByteSize = 0;
		bool IsDynamic = true;
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

		// Creates shaders, builds reflection template.
		bool Initialize(IRenderDevice* pDevice, IShaderSourceInputStreamFactory* pShaderSourceFactory, const MaterialTemplateCreateInfo& ci);

		const std::string& GetName() const { return m_Name; }
		MATERIAL_PIPELINE_TYPE GetPipelineType() const { return m_PipelineType; }

		uint32 GetShaderCount() const { return static_cast<uint32>(m_Shaders.size()); }
		IShader* GetShader(uint32 index) const { return m_Shaders[index].RawPtr(); }
		const std::vector<RefCntAutoPtr<IShader>>& GetShaders() const { return m_Shaders; }

		// Value params
		uint32 GetValueParamCount() const { return static_cast<uint32>(m_ValueParams.size()); }
		const MaterialValueParamDesc& GetValueParam(uint32 index) const { return m_ValueParams[index]; }
		const MaterialValueParamDesc* FindValueParam(const char* name) const;
		bool FindValueParamIndex(const char* name, uint32* pOutIndex) const;

		// Constant buffers
		uint32 GetCBufferCount() const { return static_cast<uint32>(m_CBuffers.size()); }
		const MaterialCBufferDesc& GetCBuffer(uint32 index) const { return m_CBuffers[index]; }

		// Resources
		uint32 GetResourceCount() const { return static_cast<uint32>(m_Resources.size()); }
		const MaterialResourceDesc& GetResource(uint32 index) const { return m_Resources[index]; }
		const MaterialResourceDesc* FindResource(const char* name) const;
		bool FindResourceIndex(const char* name, uint32* pOutIndex) const;

		bool ValidateSetValue(const char* name, MATERIAL_VALUE_TYPE expectedType, MaterialValueParamDesc* pOutDesc = nullptr) const;
		bool ValidateSetResource(const char* name, MATERIAL_RESOURCE_TYPE expectedType, MaterialResourceDesc* pOutDesc = nullptr) const;

		static constexpr const char* MATERIAL_CBUFFER_NAME = "MATERIAL_CONSTANTS";

	private:
		bool buildShaders(IRenderDevice* pDevice, IShaderSourceInputStreamFactory* pShaderSourceFactory, const std::vector<MaterialShaderStageDesc>& stages);
		bool buildReflectionFromShaders();

	private:
		MATERIAL_PIPELINE_TYPE m_PipelineType = MATERIAL_PIPELINE_TYPE_UNKNOWN;
		std::string m_Name = {};

		std::vector<RefCntAutoPtr<IShader>> m_Shaders = {};

		std::unordered_map<std::string, uint32> m_ValueParamLut = {};
		std::unordered_map<std::string, uint32> m_ResourceLut = {};

		std::vector<MaterialCBufferDesc> m_CBuffers = {};
		std::vector<MaterialValueParamDesc> m_ValueParams = {};
		std::vector<MaterialResourceDesc> m_Resources = {};
	};

} // namespace shz
