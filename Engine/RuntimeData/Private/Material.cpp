#include "pch.h"
#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	Material::Material(const std::string& name, const std::string& templateName)
		: m_Name(name)
		, m_TemplateName(templateName)
		, m_Template(m_sTemplateLibrary->at(templateName))
	{
		syncDescFromOptions();

		// Ensure runtime template binding
		{
			// Constant buffers (multi-CB)
			const uint32 cbCount = m_Template.GetCBufferCount();
			m_CBufferBlobs.resize(cbCount);

			for (uint32 i = 0; i < cbCount; ++i)
			{
				const MaterialCBufferDesc& CB = m_Template.GetCBuffer(i);

				m_CBufferBlobs[i].resize(CB.ByteSize);
				std::memset(m_CBufferBlobs[i].data(), 0, CB.ByteSize);
			}

			// Resources
			const uint32 resCount = m_Template.GetResourceCount();
			m_TextureBindings.resize(resCount);

			for (uint32 i = 0; i < resCount; ++i)
			{
				m_TextureBindings[i] = {};
			}
		}

		rebuildAutoResourceLayout();
	}

	void Material::SetBlendMode(MATERIAL_BLEND_MODE mode)
	{
		if (m_Options.BlendMode == mode) return;
		m_Options.BlendMode = mode;
		syncDescFromOptions();
	}

	void Material::SetCullMode(CULL_MODE mode)
	{
		if (m_Options.CullMode == mode) return;
		m_Options.CullMode = mode;
		syncDescFromOptions();
	}

	void Material::SetFrontCounterClockwise(bool v)
	{
		if (m_Options.FrontCounterClockwise == v) return;
		m_Options.FrontCounterClockwise = v;
		syncDescFromOptions();
	}

	void Material::SetDepthEnable(bool v)
	{
		if (m_Options.DepthEnable == v) return;
		m_Options.DepthEnable = v;
		syncDescFromOptions();
	}

	void Material::SetDepthWriteEnable(bool v)
	{
		if (m_Options.DepthWriteEnable == v) return;
		m_Options.DepthWriteEnable = v;
		syncDescFromOptions();
	}

	void Material::SetDepthFunc(COMPARISON_FUNCTION f)
	{
		if (m_Options.DepthFunc == f) return;
		m_Options.DepthFunc = f;
		syncDescFromOptions();
	}

	const uint8* Material::GetCBufferBlobData(uint32 cbufferIndex) const
	{
		ASSERT(cbufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		return m_CBufferBlobs[cbufferIndex].data();
	}

	uint32 Material::GetCBufferBlobSize(uint32 cbufferIndex) const
	{
		ASSERT(cbufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		return static_cast<uint32>(m_CBufferBlobs[cbufferIndex].size());
	}

	const MaterialValueBlob* Material::GetValueOrNull(const std::string& name) const noexcept
	{
		const auto it = m_Values.find(name);
		if (it != m_Values.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	const MaterialTexture* Material::GetTextureOrNull(const std::string& name) const noexcept
	{
		const auto it = m_Textures.find(name);
		if (it != m_Textures.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	// ---------------------------------------------------------------------
	// Write value into reflected CB blob (multi-CB)
	// ---------------------------------------------------------------------
	bool Material::writeValueImmediate(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedType)
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");
		ASSERT(pData, "pData is null.");

		MaterialValueParamDesc desc = {};
		if (!m_Template.ValidateSetValue(name, expectedType, &desc))
		{
			return false;
		}

		ASSERT(desc.CBufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		ASSERT(byteSize > 0, "Byte size must be > 0.");
		ASSERT(byteSize <= desc.ByteSize, "Byte size must be <= variable size (%u).", desc.ByteSize);

		std::vector<uint8>& blob = m_CBufferBlobs[desc.CBufferIndex];
		const uint32 endOffset = desc.ByteOffset + byteSize;
		ASSERT(endOffset <= static_cast<uint32>(blob.size()), "Out of bounds.");

		std::memcpy(blob.data() + desc.ByteOffset, pData, byteSize);

		// minimal authoring mirror (optional)
		{
			MaterialValueBlob& v = m_Values[name];
			v.Type = desc.Type;
			v.Data.resize(byteSize);
			std::memcpy(v.Data.data(), pData, byteSize);
		}

		return true;
	}

	bool Material::SetFloat(const char* name, float v) { return writeValueImmediate(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_FLOAT); }
	bool Material::SetFloat2(const char* name, const float v[2]) { return writeValueImmediate(name, v, sizeof(float) * 2, MATERIAL_VALUE_TYPE_FLOAT2); }
	bool Material::SetFloat2(const char* name, const float2& v) { return writeValueImmediate(name, &v, sizeof(float) * 2, MATERIAL_VALUE_TYPE_FLOAT2); }
	bool Material::SetFloat3(const char* name, const float v[3]) { return writeValueImmediate(name, v, sizeof(float) * 3, MATERIAL_VALUE_TYPE_FLOAT3); }
	bool Material::SetFloat3(const char* name, const float3& v) { return writeValueImmediate(name, &v, sizeof(float) * 3, MATERIAL_VALUE_TYPE_FLOAT3); }
	bool Material::SetFloat4(const char* name, const float v[4]) { return writeValueImmediate(name, v, sizeof(float) * 4, MATERIAL_VALUE_TYPE_FLOAT4); }
	bool Material::SetFloat4(const char* name, const float4& v) { return writeValueImmediate(name, &v, sizeof(float) * 4, MATERIAL_VALUE_TYPE_FLOAT4); }

	bool Material::SetInt(const char* name, int32 v) { return writeValueImmediate(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_INT); }
	bool Material::SetInt2(const char* name, const int32 v[2]) { return writeValueImmediate(name, v, sizeof(int32) * 2, MATERIAL_VALUE_TYPE_INT2); }
	bool Material::SetInt3(const char* name, const int32 v[3]) { return writeValueImmediate(name, v, sizeof(int32) * 3, MATERIAL_VALUE_TYPE_INT3); }
	bool Material::SetInt4(const char* name, const int32 v[4]) { return writeValueImmediate(name, v, sizeof(int32) * 4, MATERIAL_VALUE_TYPE_INT4); }

	bool Material::SetUint(const char* name, uint32 v) { return writeValueImmediate(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_UINT); }
	bool Material::SetUint2(const char* name, const uint32 v[2]) { return writeValueImmediate(name, v, sizeof(uint32) * 2, MATERIAL_VALUE_TYPE_UINT2); }
	bool Material::SetUint3(const char* name, const uint32 v[3]) { return writeValueImmediate(name, v, sizeof(uint32) * 3, MATERIAL_VALUE_TYPE_UINT3); }
	bool Material::SetUint4(const char* name, const uint32 v[4]) { return writeValueImmediate(name, v, sizeof(uint32) * 4, MATERIAL_VALUE_TYPE_UINT4); }

	bool Material::SetFloat4x4(const char* name, const float m16[16]) { return writeValueImmediate(name, m16, sizeof(float) * 16, MATERIAL_VALUE_TYPE_FLOAT4X4); }

	bool Material::SetRaw(const char* name, MATERIAL_VALUE_TYPE type, const void* pData, uint32 byteSize)
	{
		ASSERT(type != MATERIAL_VALUE_TYPE_UNKNOWN, "Value type is unknown. Please specify value type.");
		return writeValueImmediate(name, pData, byteSize, type);
	}

	// ---------------------------------------------------------------------
	// Textures: bind by template resource index, store simple map too.
	// ---------------------------------------------------------------------
	bool Material::setTextureImmediate(const char* name, MATERIAL_RESOURCE_TYPE expectedType, const AssetRef<Texture>& texRef)
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");
		ASSERT(IsTextureType(expectedType), "Expected type must be a texture type.");

		uint32 resIndex = 0;
		if (!m_Template.FindResourceIndex(name, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& rd = m_Template.GetResource(resIndex);
		if (!IsTextureType(rd.Type))
		{
			return false;
		}

		MaterialTextureBinding& tb = m_TextureBindings[resIndex];
		tb.Name = name;
		tb.TextureRef = texRef;

		// minimal authoring mirror
		{
			MaterialTexture& mt = m_Textures[name];
			mt.Texture = texRef;
		}

		return true;
	}

	bool Material::SetTextureAssetRef(const char* resourceName, MATERIAL_RESOURCE_TYPE expectedType, const AssetRef<Texture>& textureRef)
	{
		return setTextureImmediate(resourceName, expectedType, textureRef);
	}

	bool Material::SetSamplerOverridePtr(const char* resourceName, ISampler* pSampler)
	{
		ASSERT(resourceName && resourceName[0] != '\0', "Invalid name string.");

		uint32 resIndex = 0;
		if (!m_Template.FindResourceIndex(resourceName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& rd = m_Template.GetResource(resIndex);
		if (!IsTextureType(rd.Type))
		{
			return false;
		}

		MaterialTextureBinding& tb = m_TextureBindings[resIndex];
		tb.Name = resourceName;
		tb.pSamplerOverride = pSampler;

		return true;
	}

	bool Material::SetSamplerOverrideDesc(const char* resourceName, const SamplerDesc& desc)
	{
		ASSERT(resourceName && resourceName[0] != '\0', "Invalid name string.");

		uint32 resIndex = 0;
		if (!m_Template.FindResourceIndex(resourceName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& rd = m_Template.GetResource(resIndex);
		if (!IsTextureType(rd.Type))
		{
			return false;
		}

		MaterialTextureBinding& tb = m_TextureBindings[resIndex];
		tb.Name = resourceName;
		tb.bHasSamplerOverride = true;
		tb.SamplerOverrideDesc = desc;
		tb.pSamplerOverride = nullptr;

		return true;
	}

	bool Material::ClearSamplerOverride(const char* resourceName)
	{
		ASSERT(resourceName && resourceName[0] != '\0', "Invalid name string.");

		uint32 resIndex = 0;
		if (!m_Template.FindResourceIndex(resourceName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& rd = m_Template.GetResource(resIndex);
		if (!IsTextureType(rd.Type))
		{
			return false;
		}

		MaterialTextureBinding& tb = m_TextureBindings[resIndex];
		tb.bHasSamplerOverride = false;
		tb.pSamplerOverride = nullptr;

		return true;
	}

	GraphicsPipelineStateCreateInfo Material::BuildGraphicsPipelineStateCreateInfo(IRenderPass* pRenderPass) const
	{
		ASSERT(pRenderPass, "RenderPass is null.");

		GraphicsPipelineStateCreateInfo outGraphicsPipelineStateCI = {};

		PipelineStateDesc& psDesc = outGraphicsPipelineStateCI.PSODesc;
		psDesc = m_PipelineStateDesc;

		GraphicsPipelineDesc& gpDesc = outGraphicsPipelineStateCI.GraphicsPipeline;
		gpDesc = m_GraphicsPipelineDesc;

		// Inject pRenderPass if graphics pipeline
		if (psDesc.IsAnyGraphicsPipeline())
		{
			GraphicsPipelineDesc* gp = &gpDesc;
			ASSERT(gp, "Graphics pipeline desc is required for graphics PSO.");

			gp->pRenderPass = nullptr;
			gp->SubpassIndex = 0;
			gp->pRenderPass = pRenderPass;

			gp->NumRenderTargets = 0;
			for (uint32 i = 0; i < _countof(gp->RTVFormats); ++i)
			{
				gp->RTVFormats[i] = TEX_FORMAT_UNKNOWN;
			}
			gp->DSVFormat = TEX_FORMAT_UNKNOWN;
			gp->ReadOnlyDSV = false;
		}

		// Attach shaders from instance
		bool bHasMeshStages = false;
		bool bHasLegacyStages = false;

		for (const RefCntAutoPtr<IShader>& shader : GetShaders())
		{
			ASSERT(shader, "Shader in source instance is null.");

			const SHADER_TYPE shaderType = shader->GetDesc().ShaderType;

			if (shaderType == SHADER_TYPE_MESH || shaderType == SHADER_TYPE_AMPLIFICATION)
			{
				bHasMeshStages = true;
			}

			if (shaderType == SHADER_TYPE_VERTEX ||
				shaderType == SHADER_TYPE_GEOMETRY ||
				shaderType == SHADER_TYPE_HULL ||
				shaderType == SHADER_TYPE_DOMAIN)
			{
				bHasLegacyStages = true;
			}

			if (shaderType == SHADER_TYPE_VERTEX)             outGraphicsPipelineStateCI.pVS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_PIXEL)         outGraphicsPipelineStateCI.pPS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_GEOMETRY)      outGraphicsPipelineStateCI.pGS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_HULL)          outGraphicsPipelineStateCI.pHS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_DOMAIN)        outGraphicsPipelineStateCI.pDS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_AMPLIFICATION) outGraphicsPipelineStateCI.pAS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_MESH)          outGraphicsPipelineStateCI.pMS = shader.RawPtr();
		}

		ASSERT(!(bHasMeshStages && bHasLegacyStages), "Invalid shader stage mix: mesh stages can't be combined with VS/GS/HS/DS.");

		return outGraphicsPipelineStateCI;
	}

	ComputePipelineStateCreateInfo Material::BuildComputePipelineStateCreateInfo() const
	{
		ComputePipelineStateCreateInfo outComputePipelineStateCI = {};
		PipelineStateDesc& psDesc = outComputePipelineStateCI.PSODesc;
		psDesc = m_PipelineStateDesc;

		for (const RefCntAutoPtr<IShader>& shader : GetShaders())
		{
			ASSERT(shader, "Shader in source instance is null.");
			const SHADER_TYPE shaderType = shader->GetDesc().ShaderType;
			if (shaderType == SHADER_TYPE_COMPUTE)
			{
				outComputePipelineStateCI.pCS = shader.RawPtr();
			}
		}

		return outComputePipelineStateCI;
	}

	void Material::Clear()
	{
		m_Name.clear();
		m_TemplateName.clear();

		m_Options = {};

		m_PipelineStateDesc = {};
		m_GraphicsPipelineDesc = {};

		m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		m_Variables.clear();
		m_ImmutableSamplersStorage.clear();

		m_CBufferBlobs.clear();
		m_TextureBindings.clear();

		m_Values.clear();
		m_Textures.clear();
	}

	// ---------------------------------------------------------------------
	// IMPORTANT: Multi-CB layout (this is what you asked)
	// - add variable entries for ALL reflected CBs (not just MATERIAL_CONSTANTS)
	// - keep the rest same as your original layout policy
	// ---------------------------------------------------------------------
	void Material::rebuildAutoResourceLayout()
	{
		m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		m_Variables.clear();
		m_ImmutableSamplersStorage.clear();

		m_Variables.reserve(32);
		m_ImmutableSamplersStorage.reserve(4);

		// Constant buffers (ALL)
		{
			const uint32 cbCount = m_Template.GetCBufferCount();
			for (uint32 i = 0; i < cbCount; ++i)
			{
				const MaterialCBufferDesc& cb = m_Template.GetCBuffer(i);

				ShaderResourceVariableDesc v = {};
				v.ShaderStages = cb.ShaderStages;
				v.Name = cb.Name.c_str();
				v.Type = cb.IsDynamic ? SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC : SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
				m_Variables.push_back(v);
			}
		}

		// Textures
		const uint32 resCount = m_Template.GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& r = m_Template.GetResource(i);

			if (IsTextureType(r.Type))
			{
				ShaderResourceVariableDesc v = {};
				v.ShaderStages = r.ShaderStages;
				v.Name = r.Name.c_str();
				v.Type = r.IsDynamic ? SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC : SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
				m_Variables.push_back(v);
			}
		}

		// Immutable sampler: LinearWrap
		{
			// Fixed immutable sampler
			SamplerDesc linearWrapSamplerDesc =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			SamplerDesc linearClampSamplerDesc =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
			};

			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrapSamplerDesc));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_VERTEX, "g_LinearWrapSampler", linearWrapSamplerDesc));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClampSamplerDesc));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_VERTEX, "g_LinearClampSampler", linearClampSamplerDesc));
		}

		// Write into PSODesc.ResourceLayout (plain struct)
		{
			PipelineResourceLayoutDesc& rl = m_PipelineStateDesc.ResourceLayout;
			rl = {};

			rl.DefaultVariableType = m_DefaultVariableType;

			rl.Variables = m_Variables.empty() ? nullptr : m_Variables.data();
			rl.NumVariables = static_cast<uint32>(m_Variables.size());

			rl.ImmutableSamplers = m_ImmutableSamplersStorage.empty() ? nullptr : m_ImmutableSamplersStorage.data();
			rl.NumImmutableSamplers = static_cast<uint32>(m_ImmutableSamplersStorage.size());
		}
	}

	void Material::syncDescFromOptions()
	{
		m_PipelineStateDesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		{
			if (!m_Name.empty())
			{
				m_PipelineStateDesc.Name = m_Name.c_str();
			}
			else if (!m_Template.GetName().empty())
			{
				m_PipelineStateDesc.Name = GetName().c_str();
			}
			else
			{
				m_PipelineStateDesc.Name = "Material PSO";
			}
		}

		if (m_PipelineStateDesc.IsAnyGraphicsPipeline())
		{
			m_GraphicsPipelineDesc.NumRenderTargets = 0;
			for (uint32 i = 0; i < _countof(m_GraphicsPipelineDesc.RTVFormats); ++i)
				m_GraphicsPipelineDesc.RTVFormats[i] = TEX_FORMAT_UNKNOWN;
			m_GraphicsPipelineDesc.DSVFormat = TEX_FORMAT_UNKNOWN;

			m_GraphicsPipelineDesc.pRenderPass = nullptr;
			m_GraphicsPipelineDesc.SubpassIndex = 0;

			m_GraphicsPipelineDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			// Raster
			{
				m_GraphicsPipelineDesc.RasterizerDesc.CullMode = m_Options.CullMode;
				m_GraphicsPipelineDesc.RasterizerDesc.FrontCounterClockwise = m_Options.FrontCounterClockwise;
			}

			// Depth
			{
				m_GraphicsPipelineDesc.DepthStencilDesc.DepthEnable = m_Options.DepthEnable;
				m_GraphicsPipelineDesc.DepthStencilDesc.DepthWriteEnable = m_Options.DepthWriteEnable;
				m_GraphicsPipelineDesc.DepthStencilDesc.DepthFunc = m_Options.DepthFunc;
			}

			static LayoutElement kLayoutElems[] =
			{
				LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
				LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
				LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
				LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
			};

			m_GraphicsPipelineDesc.InputLayout.LayoutElements = kLayoutElems;
			m_GraphicsPipelineDesc.InputLayout.NumElements = _countof(kLayoutElems);
		}
	}
} // namespace shz
