// ============================================================================
// Engine/Material/Public/MaterialInstance.h
//   - Owns shaders + reflection template + PSO creation parameters.
//   - MaterialRenderData can create PSO/SRB immediately from this instance.
// ============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Public/AssetRef.hpp"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/ISampler.h"
#include "Engine/RHI/Interface/IRenderPass.h"

#include "Engine/Material/Public/MaterialTemplate.h"

namespace shz
{
	struct TextureBinding final
	{
		std::string Name = {};

		AssetRef<TextureAsset> TextureRef = {};

		ITextureView* pRuntimeView = nullptr;
		ISampler* pSamplerOverride = nullptr;
	};

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
		SHADER_COMPILE_FLAGS CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		// Keep consistent with your renderer style.
		bool UseCombinedTextureSamplers = false;
	};

	struct MaterialResourceLayoutDesc final
	{
		// If you do not provide Variables/ImmutableSamplers, they will be auto-generated
		// from the reflected MaterialTemplate by default policy:
		// - MATERIAL_CONSTANTS : DYNAMIC
		// - texture SRVs       : MUTABLE
		// - others             : STATIC (unless you specify otherwise)
		SHADER_RESOURCE_VARIABLE_TYPE DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		std::vector<ShaderResourceVariableDesc> Variables = {};
		std::vector<ImmutableSamplerDesc> ImmutableSamplers = {};
	};

	struct MaterialGraphicsPsoDesc final
	{
		std::string Name = "Material Graphics PSO";

		GraphicsPipelineDesc GraphicsPipeline = {};
		PipelineStateDesc PSODesc = {};

		MaterialGraphicsPsoDesc()
		{
			PSODesc.Name = Name.c_str();
			PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		}
	};

	struct MaterialComputePsoDesc final
	{
		std::string Name = "Material Compute PSO";

		PipelineStateDesc PSODesc = {};

		MaterialComputePsoDesc()
		{
			PSODesc.Name = Name.c_str();
			PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
		}
	};

	struct MaterialInstanceCreateInfo final
	{
		MATERIAL_PIPELINE_TYPE PipelineType = MATERIAL_PIPELINE_TYPE_UNKNOWN;

		MaterialResourceLayoutDesc ResourceLayout = {};

		MaterialGraphicsPsoDesc Graphics = {};
		MaterialComputePsoDesc Compute = {};

		std::vector<MaterialShaderStageDesc> ShaderStages = {};

		// Optional: name to store into template for better error messages.
		std::string TemplateName = {};
	};

	class MaterialInstance final
	{
	public:
		MaterialInstance() = default;
		~MaterialInstance() = default;

		MaterialInstance(const MaterialInstance&) = default;
		MaterialInstance& operator=(const MaterialInstance&) = default;

		MaterialInstance(MaterialInstance&&) noexcept = default;
		MaterialInstance& operator=(MaterialInstance&&) noexcept = default;

		// Creates shaders, builds reflection template, prepares PSO create parameters,
		// and allocates per-instance constant blobs/bindings.
		bool Initialize(IRenderDevice* pDevice, IShaderSourceInputStreamFactory* pShaderSourceFactory, const MaterialInstanceCreateInfo& ci);

		MATERIAL_PIPELINE_TYPE GetPipelineType() const { return m_PipelineType; }

		const MaterialTemplate* GetTemplate() const { return &m_Template; }

		const MaterialResourceLayoutDesc& GetResourceLayout() const { return m_ResourceLayout; }

		const MaterialGraphicsPsoDesc& GetGraphicsPsoDesc() const { return m_GraphicsPso; }
		const MaterialComputePsoDesc& GetComputePsoDesc() const { return m_ComputePso; }

		uint32 GetShaderCount() const { return static_cast<uint32>(m_Shaders.size()); }
		IShader* GetShader(uint32 index) const { return m_Shaders[index].RawPtr(); }
		const std::vector<RefCntAutoPtr<IShader>>& GetShaders() const { return m_Shaders; }

		// ------------------------------------------------------------
		// Values
		// ------------------------------------------------------------
		bool SetFloat(const char* name, float v);
		bool SetFloat2(const char* name, const float v[2]);
		bool SetFloat3(const char* name, const float v[3]);
		bool SetFloat4(const char* name, const float v[4]);

		bool SetInt(const char* name, int32 v);
		bool SetInt2(const char* name, const int32 v[2]);
		bool SetInt3(const char* name, const int32 v[3]);
		bool SetInt4(const char* name, const int32 v[4]);

		bool SetUint(const char* name, uint32 v);
		bool SetUint2(const char* name, const uint32 v[2]);
		bool SetUint3(const char* name, const uint32 v[3]);
		bool SetUint4(const char* name, const uint32 v[4]);

		bool SetFloat4x4(const char* name, const float m16[16]);

		bool SetRaw(const char* name, const void* pData, uint32 byteSize);

		// ------------------------------------------------------------
		// Resources
		// ------------------------------------------------------------
		bool SetTextureAssetRef(const char* textureName, const AssetRef<TextureAsset>& textureRef);
		bool SetTextureRuntimeView(const char* textureName, ITextureView* pView);

		bool SetSamplerOverride(const char* textureName, ISampler* pSampler);

		// ------------------------------------------------------------
		// For MaterialRenderData
		// ------------------------------------------------------------
		uint32 GetCBufferBlobCount() const { return static_cast<uint32>(m_CBufferBlobs.size()); }
		const uint8* GetCBufferBlobData(uint32 cbufferIndex) const;
		uint32 GetCBufferBlobSize(uint32 cbufferIndex) const;

		bool IsCBufferDirty(uint32 cbufferIndex) const;
		void ClearCBufferDirty(uint32 cbufferIndex);

		uint32 GetTextureBindingCount() const { return static_cast<uint32>(m_TextureBindings.size()); }
		const TextureBinding& GetTextureBinding(uint32 index) const { return m_TextureBindings[index]; }

		bool IsTextureDirty(uint32 resourceIndex) const;
		void ClearTextureDirty(uint32 resourceIndex);

		void MarkAllDirty();

	private:
		bool writeValueInternal(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedValueType);

		bool buildShaders(IRenderDevice* pDevice, IShaderSourceInputStreamFactory* pShaderSourceFactory, const std::vector<MaterialShaderStageDesc>& stages);
		bool buildTemplateFromShaders(const std::string& templateName);
		void buildDefaultResourceLayoutIfEmpty();

		static inline bool isTextureType(MATERIAL_RESOURCE_TYPE t)
		{
			return (t == MATERIAL_RESOURCE_TYPE_TEXTURE2D) ||
				(t == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ||
				(t == MATERIAL_RESOURCE_TYPE_TEXTURECUBE);
		}

	private:
		MATERIAL_PIPELINE_TYPE m_PipelineType = MATERIAL_PIPELINE_TYPE_UNKNOWN;

		MaterialTemplate m_Template = {};
		std::vector<RefCntAutoPtr<IShader>> m_Shaders = {};

		MaterialResourceLayoutDesc m_ResourceLayout = {};
		MaterialGraphicsPsoDesc m_GraphicsPso = {};
		MaterialComputePsoDesc m_ComputePso = {};

		std::vector<std::vector<uint8>> m_CBufferBlobs = {};
		std::vector<uint8> m_bCBufferDirties = {};

		std::vector<TextureBinding> m_TextureBindings = {};
		std::vector<uint8> m_bTextureDirties = {};
	};

} // namespace shz
