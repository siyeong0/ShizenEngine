// ============================================================================
// Engine/Material/Private/MaterialInstance.cpp
// ============================================================================
#include "pch.h"
#include "Engine/Material/Public/MaterialInstance.h"

#include <cstring>
#include <algorithm>

namespace shz
{
	static inline const char* toPsoNameStorage(const std::string& s)
	{
		return s.empty() ? "" : s.c_str();
	}

	bool MaterialInstance::Initialize(IRenderDevice* pDevice, IShaderSourceInputStreamFactory* pShaderSourceFactory, const MaterialInstanceCreateInfo& ci)
	{
		ASSERT(pDevice, "Device is null.");
		ASSERT(pShaderSourceFactory, "Shader source factory is null.");

		m_PipelineType = ci.PipelineType;
		m_ResourceLayout = ci.ResourceLayout;
		m_GraphicsPso = ci.Graphics;
		m_ComputePso = ci.Compute;

		m_Shaders.clear();
		m_CBufferBlobs.clear();
		m_bCBufferDirties.clear();
		m_TextureBindings.clear();
		m_bTextureDirties.clear();

		ASSERT(m_PipelineType != MATERIAL_PIPELINE_TYPE_UNKNOWN, "Invalid pipeline type.");
		ASSERT(!ci.ShaderStages.empty(), "No shader stages were provided.");

		if (!buildShaders(pDevice, pShaderSourceFactory, ci.ShaderStages))
		{
			return false;
		}

		if (!buildTemplateFromShaders(ci.TemplateName))
		{
			return false;
		}

		buildDefaultResourceLayoutIfEmpty();

		// Allocate CB blobs (usually 1: MATERIAL_CONSTANTS)
		const uint32 cbCount = m_Template.GetCBufferCount();
		m_CBufferBlobs.resize(cbCount);
		m_bCBufferDirties.resize(cbCount);

		for (uint32 i = 0; i < cbCount; ++i)
		{
			const auto& CB = m_Template.GetCBuffer(i);
			m_CBufferBlobs[i].resize(CB.ByteSize);
			std::memset(m_CBufferBlobs[i].data(), 0, CB.ByteSize);
			m_bCBufferDirties[i] = 1;
		}

		// Allocate bindings aligned with template resources
		const uint32 resCount = m_Template.GetResourceCount();
		m_TextureBindings.resize(resCount);
		m_bTextureDirties.resize(resCount);

		for (uint32 i = 0; i < resCount; ++i)
		{
			m_TextureBindings[i] = {};
			m_bTextureDirties[i] = 1;
		}

		MarkAllDirty();
		return true;
	}

	bool MaterialInstance::buildShaders(IRenderDevice* pDevice, IShaderSourceInputStreamFactory* pShaderSourceFactory, const std::vector<MaterialShaderStageDesc>& stages)
	{
		ASSERT(pDevice, "Device is null.");
		ASSERT(pShaderSourceFactory, "Shader source factory is null.");

		m_Shaders.clear();
		m_Shaders.reserve(stages.size());

		ShaderCreateInfo sci = {};
		sci.pShaderSourceStreamFactory = pShaderSourceFactory;

		for (const MaterialShaderStageDesc& s : stages)
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

		return true;
	}

	bool MaterialInstance::buildTemplateFromShaders(const std::string& templateName)
	{
		std::vector<const IShader*> ptrs = {};
		ptrs.reserve(m_Shaders.size());

		for (const RefCntAutoPtr<IShader>& s : m_Shaders)
		{
			ptrs.push_back(s.RawPtr());
		}

		m_Template = {};
		m_Template.SetName(templateName.empty() ? "MaterialTemplate" : templateName);

		if (!m_Template.BuildFromShaders(ptrs))
		{
			return false;
		}

		return true;
	}

	void MaterialInstance::buildDefaultResourceLayoutIfEmpty()
	{
		if (!m_ResourceLayout.Variables.empty() || !m_ResourceLayout.ImmutableSamplers.empty())
		{
			return;
		}

		// Default policy:
		// - MATERIAL_CONSTANTS : DYNAMIC
		// - texture SRVs       : MUTABLE
		// - everything else    : STATIC (via DefaultVariableType)
		m_ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		std::vector<ShaderResourceVariableDesc> vars = {};

		// MATERIAL_CONSTANTS is expected in PS/VS commonly.
		// We add for all stages by default to be safe (Diligent ignores missing ones).
		{
			ShaderResourceVariableDesc v = {};
			v.ShaderStages = SHADER_TYPE_ALL;
			v.Name = MaterialTemplate::MATERIAL_CBUFFER_NAME;
			v.Type = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
			vars.push_back(v);
		}

		const uint32 resCount = m_Template.GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& r = m_Template.GetResource(i);

			if (isTextureType(r.Type))
			{
				ShaderResourceVariableDesc v = {};
				v.ShaderStages = SHADER_TYPE_ALL;
				v.Name = r.Name.c_str();
				v.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
				vars.push_back(v);
			}
		}

		m_ResourceLayout.Variables = std::move(vars);
	}

	const uint8* MaterialInstance::GetCBufferBlobData(uint32 cbufferIndex) const
	{
		ASSERT(cbufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		return m_CBufferBlobs[cbufferIndex].data();
	}

	uint32 MaterialInstance::GetCBufferBlobSize(uint32 cbufferIndex) const
	{
		ASSERT(cbufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		return static_cast<uint32>(m_CBufferBlobs[cbufferIndex].size());
	}

	bool MaterialInstance::IsCBufferDirty(uint32 cbufferIndex) const
	{
		ASSERT(cbufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		return m_bCBufferDirties[cbufferIndex] != 0;
	}

	void MaterialInstance::ClearCBufferDirty(uint32 cbufferIndex)
	{
		ASSERT(cbufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		m_bCBufferDirties[cbufferIndex] = 0;
	}

	bool MaterialInstance::IsTextureDirty(uint32 resourceIndex) const
	{
		ASSERT(resourceIndex < static_cast<uint32>(m_bTextureDirties.size()), "Out of bounds.");
		return m_bTextureDirties[resourceIndex] != 0;
	}

	void MaterialInstance::ClearTextureDirty(uint32 resourceIndex)
	{
		ASSERT(resourceIndex < static_cast<uint32>(m_bTextureDirties.size()), "Out of bounds.");
		m_bTextureDirties[resourceIndex] = 0;
	}

	void MaterialInstance::MarkAllDirty()
	{
		for (uint8& b : m_bCBufferDirties)
		{
			b = 1;
		}
		for (uint8& b : m_bTextureDirties)
		{
			b = 1;
		}
	}

	bool MaterialInstance::writeValueInternal(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedValueType)
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");
		ASSERT(pData, "pData is null.");

		MaterialValueParamDesc desc = {};
		if (!m_Template.ValidateSetValue(name, expectedValueType, &desc))
		{
			ASSERTION_FAILED("There is no value named %s in shader template %s.", name, m_Template.GetName().c_str());
			return false;
		}

		ASSERT(desc.CBufferIndex < m_CBufferBlobs.size(), "Out of bounds.");
		ASSERT(byteSize > 0, "Byte size must be bigger than 0.");
		ASSERT(byteSize <= desc.ByteSize, "Byte size must be <= variable size (%u).", desc.ByteSize);

		std::vector<uint8>& blob = m_CBufferBlobs[desc.CBufferIndex];
		const uint32 endOffset = static_cast<uint32>(desc.ByteOffset) + byteSize;

		ASSERT(endOffset <= static_cast<uint32>(blob.size()), "Out of bounds.");

		std::memcpy(blob.data() + desc.ByteOffset, pData, byteSize);
		m_bCBufferDirties[desc.CBufferIndex] = 1;

		return true;
	}

	bool MaterialInstance::SetRaw(const char* name, const void* pData, uint32 byteSize)
	{
		return writeValueInternal(name, pData, byteSize, MATERIAL_VALUE_TYPE_UNKNOWN);
	}

	bool MaterialInstance::SetFloat(const char* name, float v)
	{
		return writeValueInternal(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_FLOAT);
	}

	bool MaterialInstance::SetFloat2(const char* name, const float v[2])
	{
		return writeValueInternal(name, v, sizeof(float) * 2, MATERIAL_VALUE_TYPE_FLOAT2);
	}

	bool MaterialInstance::SetFloat3(const char* name, const float v[3])
	{
		return writeValueInternal(name, v, sizeof(float) * 3, MATERIAL_VALUE_TYPE_FLOAT3);
	}

	bool MaterialInstance::SetFloat4(const char* name, const float v[4])
	{
		return writeValueInternal(name, v, sizeof(float) * 4, MATERIAL_VALUE_TYPE_FLOAT4);
	}

	bool MaterialInstance::SetInt(const char* name, int32 v)
	{
		return writeValueInternal(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_INT);
	}

	bool MaterialInstance::SetInt2(const char* name, const int32 v[2])
	{
		return writeValueInternal(name, v, sizeof(int32) * 2, MATERIAL_VALUE_TYPE_INT2);
	}

	bool MaterialInstance::SetInt3(const char* name, const int32 v[3])
	{
		return writeValueInternal(name, v, sizeof(int32) * 3, MATERIAL_VALUE_TYPE_INT3);
	}

	bool MaterialInstance::SetInt4(const char* name, const int32 v[4])
	{
		return writeValueInternal(name, v, sizeof(int32) * 4, MATERIAL_VALUE_TYPE_INT4);
	}

	bool MaterialInstance::SetUint(const char* name, uint32 v)
	{
		return writeValueInternal(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_UINT);
	}

	bool MaterialInstance::SetUint2(const char* name, const uint32 v[2])
	{
		return writeValueInternal(name, v, sizeof(uint32) * 2, MATERIAL_VALUE_TYPE_UINT2);
	}

	bool MaterialInstance::SetUint3(const char* name, const uint32 v[3])
	{
		return writeValueInternal(name, v, sizeof(uint32) * 3, MATERIAL_VALUE_TYPE_UINT3);
	}

	bool MaterialInstance::SetUint4(const char* name, const uint32 v[4])
	{
		return writeValueInternal(name, v, sizeof(uint32) * 4, MATERIAL_VALUE_TYPE_UINT4);
	}

	bool MaterialInstance::SetFloat4x4(const char* name, const float m16[16])
	{
		return writeValueInternal(name, m16, sizeof(float) * 16, MATERIAL_VALUE_TYPE_FLOAT4X4);
	}

	bool MaterialInstance::SetTextureAssetRef(const char* textureName, const AssetRef<TextureAsset>& textureRef)
	{
		ASSERT(textureName && textureName[0] != '\0', "Invalid name.");

		uint32 resIndex = 0;
		if (!m_Template.FindResourceIndex(textureName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& resourceDesc = m_Template.GetResource(resIndex);
		if (!isTextureType(resourceDesc.Type))
		{
			return false;
		}

		m_TextureBindings[resIndex].Name = textureName;
		m_TextureBindings[resIndex].TextureRef = textureRef;
		m_TextureBindings[resIndex].pRuntimeView = nullptr;

		m_bTextureDirties[resIndex] = 1;
		return true;
	}

	bool MaterialInstance::SetTextureRuntimeView(const char* textureName, ITextureView* pView)
	{
		ASSERT(textureName && textureName[0] != '\0', "Invalid name.");

		uint32 resIndex = 0;
		if (!m_Template.FindResourceIndex(textureName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& resourceDesc = m_Template.GetResource(resIndex);
		if (!isTextureType(resourceDesc.Type))
		{
			return false;
		}

		m_TextureBindings[resIndex].Name = textureName;
		m_TextureBindings[resIndex].pRuntimeView = pView;

		m_bTextureDirties[resIndex] = 1;
		return true;
	}

	bool MaterialInstance::SetSamplerOverride(const char* textureName, ISampler* pSampler)
	{
		ASSERT(textureName && textureName[0] != '\0', "Invalid name.");

		uint32 resIndex = 0;
		if (!m_Template.FindResourceIndex(textureName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& resourceDesc = m_Template.GetResource(resIndex);
		if (!isTextureType(resourceDesc.Type))
		{
			return false;
		}

		m_TextureBindings[resIndex].Name = textureName;
		m_TextureBindings[resIndex].pSamplerOverride = pSampler;

		m_bTextureDirties[resIndex] = 1;
		return true;
	}

} // namespace shz
