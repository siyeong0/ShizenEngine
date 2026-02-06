#pragma once
#include <vector>
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RuntimeData/Public/MaterialTypes.h"

namespace shz
{
	using ShaderId = uint64;

	class Shader final
	{
	public:
		struct StageDesc final
		{
			SHADER_TYPE ShaderType = SHADER_TYPE_UNKNOWN;

			std::string DebugName = {};
			std::string FilePath = {};
			std::string EntryPoint = "main";

			SHADER_SOURCE_LANGUAGE SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			SHADER_COMPILE_FLAGS CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

			bool UseCombinedTextureSamplers = false;
		};

		struct ValueParamDesc final
		{
			std::string Name = {};
			MATERIAL_VALUE_TYPE Type = MATERIAL_VALUE_TYPE_UNKNOWN;

			uint32 CBufferIndex = 0;
			uint32 ByteOffset = 0;
			uint32 ByteSize = 0;

			MaterialParamFlags Flags = MaterialParamFlags_None;
		};

		struct CBufferDesc final
		{
			std::string Name = {};
			uint32 ByteSize = 0;
			bool IsDynamic = true;
		};

		struct ResourceDesc final
		{
			std::string Name = {};
			MATERIAL_RESOURCE_TYPE Type = MATERIAL_RESOURCE_TYPE_UNKNOWN;
			uint16 ArraySize = 1;
			bool IsDynamic = true;
		};
	public:
		Shader(const std::vector<StageDesc>& stages);
		~Shader() = default;

		Shader() = delete;
		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		Shader(Shader&&) noexcept = default;
		Shader& operator=(Shader&&) noexcept = default;

		static void RegisterRenderer(class Renderer* pRenderer) { s_pRenderer = pRenderer; }

		IShader* GetShaderOrNull(SHADER_TYPE type) const { return m_Shaders.at(type).RawPtr(); }

		// Value params
		uint32 GetValueParamCount() const { return static_cast<uint32>(m_ValueParams.size()); }
		const ValueParamDesc& GetValueParam(uint32 index) const { return m_ValueParams[index]; }
		const ValueParamDesc* FindValueParam(const std::string& name) const;
		bool FindValueParamIndex(const char* name, uint32* pOutIndex) const;

		// Constant buffers
		uint32 GetCBufferCount() const { return static_cast<uint32>(m_CBuffers.size()); }
		const CBufferDesc& GetCBuffer(uint32 index) const { return m_CBuffers[index]; }

		// Resources
		uint32 GetResourceCount() const { return static_cast<uint32>(m_Resources.size()); }
		const ResourceDesc& GetResource(uint32 index) const { return m_Resources[index]; }
		const ResourceDesc* FindResource(const char* name) const;
		bool FindResourceIndex(const char* name, uint32* pOutIndex) const;

	private:

	private:
		inline static class Renderer* s_pRenderer = nullptr;

		std::unordered_map<SHADER_TYPE, RefCntAutoPtr<IShader>> m_Shaders;

		std::unordered_map<std::string, uint32> m_ValueParamLut = {};
		std::unordered_map<std::string, uint32> m_ResourceLut = {};

		std::vector<CBufferDesc> m_CBuffers = {};
		std::vector<ValueParamDesc> m_ValueParams = {};
		std::vector<ResourceDesc> m_Resources = {};
	};

} // namespace shz
