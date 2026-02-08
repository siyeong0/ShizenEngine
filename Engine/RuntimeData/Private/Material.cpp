#include "pch.h"
#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	Material::Material(const std::string& name, const std::string& templateName)
		: m_Name(name)
		, m_TemplateName(templateName)
		, m_Template(m_sTemplateLibrary->at(templateName))
	{
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

		// Immutable samplers (fixed)
		{
			SamplerDesc PointWrapSampler =
			{
				FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			SamplerDesc PointClampSampler =
			{
				FILTER_TYPE_POINT, FILTER_TYPE_POINT, FILTER_TYPE_POINT,
				TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
			};

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

			SamplerDesc ShadowCmpSampler =
			{
				FILTER_TYPE_COMPARISON_LINEAR, FILTER_TYPE_COMPARISON_LINEAR, FILTER_TYPE_COMPARISON_LINEAR,
				TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
			};
			ShadowCmpSampler.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;

			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_PIXEL, "g_PointWrapSampler", PointWrapSampler));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_VERTEX, "g_PointWrapSampler", PointWrapSampler));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_PIXEL, "g_PointClampSampler", PointClampSampler));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_VERTEX, "g_PointClampSampler", PointClampSampler));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrapSamplerDesc));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_VERTEX, "g_LinearWrapSampler", linearWrapSamplerDesc));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClampSamplerDesc));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_VERTEX, "g_LinearClampSampler", linearClampSamplerDesc));
			m_ImmutableSamplersStorage.push_back(ImmutableSamplerDesc(SHADER_TYPE_PIXEL, "g_ShadowCmpSampler", ShadowCmpSampler));
		}
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
	// Textures
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

		return true;
	}

	// ---------------------------------------------------------------------
	// Build graphics PSO create info (no persistent desc state)
	// ---------------------------------------------------------------------
	GraphicsPipelineStateCreateInfo Material::BuildGraphicsPipelineStateCreateInfo(IRenderPass* pRenderPass) const
	{
		ASSERT(pRenderPass, "RenderPass is null.");

		GraphicsPipelineStateCreateInfo outCI = {};

		// -----------------------------
		// PSODesc
		// -----------------------------
		PipelineStateDesc& psDesc = outCI.PSODesc;
		psDesc = {};
		psDesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		ASSERT(!m_Name.empty(), "Material name is empty.");
		psDesc.Name = m_Name.c_str();

		// Resource layout (from cached arrays)
		{
			PipelineResourceLayoutDesc& rl = psDesc.ResourceLayout;
			rl = {};
			rl.DefaultVariableType = GetDefaultVariableType();

			rl.Variables = GetLayoutVarCount() > 0 ? m_Variables.data() : nullptr;
			rl.NumVariables = GetLayoutVarCount();

			rl.ImmutableSamplers = GetImmutableSamplerCount() > 0 ? m_ImmutableSamplersStorage.data() : nullptr;
			rl.NumImmutableSamplers = GetImmutableSamplerCount();
		}

		// -----------------------------
		// GraphicsPipeline
		// -----------------------------
		GraphicsPipelineDesc& gpDesc = outCI.GraphicsPipeline;
		gpDesc = {};

		// RenderPass injection
		gpDesc.pRenderPass = pRenderPass;
		gpDesc.SubpassIndex = 0;

		// NOTE: RTV/DSV formats are typically derived from render pass in RP-compatible pipelines.
		// Keep them unknown here to avoid material owning pass formats.
		gpDesc.NumRenderTargets = 0;
		for (uint32 i = 0; i < _countof(gpDesc.RTVFormats); ++i)
		{
			gpDesc.RTVFormats[i] = TEX_FORMAT_UNKNOWN;
		}
		gpDesc.DSVFormat = TEX_FORMAT_UNKNOWN;
		gpDesc.ReadOnlyDSV = false;

		gpDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Raster options
		{
			gpDesc.RasterizerDesc.CullMode = GetCullMode();
			gpDesc.RasterizerDesc.FrontCounterClockwise = GetFrontCounterClockwise();
		}

		// Depth options
		{
			gpDesc.DepthStencilDesc.DepthEnable = GetDepthEnable();
			gpDesc.DepthStencilDesc.DepthWriteEnable = GetDepthWriteEnable();
			gpDesc.DepthStencilDesc.DepthFunc = GetDepthFunc();
		}

		// Input layout (keep as you had it)
		{
			static LayoutElement FIXED_LAYOUT_ELEMENTS[] =
			{
				LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
				LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
				LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
				LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
			};

			gpDesc.InputLayout.LayoutElements = FIXED_LAYOUT_ELEMENTS;
			gpDesc.InputLayout.NumElements = _countof(FIXED_LAYOUT_ELEMENTS);
		}

		// -----------------------------
		// Attach shaders from template
		// -----------------------------
		bool bHasMeshStages = false;
		bool bHasLegacyStages = false;

		for (const RefCntAutoPtr<IShader>& shader : GetShaders())
		{
			ASSERT(shader, "Shader in template is null.");

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

			if (shaderType == SHADER_TYPE_VERTEX)             outCI.pVS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_PIXEL)         outCI.pPS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_GEOMETRY)      outCI.pGS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_HULL)          outCI.pHS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_DOMAIN)        outCI.pDS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_AMPLIFICATION) outCI.pAS = shader.RawPtr();
			else if (shaderType == SHADER_TYPE_MESH)          outCI.pMS = shader.RawPtr();
		}

		ASSERT(!(bHasMeshStages && bHasLegacyStages), "Invalid shader stage mix: mesh stages can't be combined with VS/GS/HS/DS.");

		return outCI;
	}

	void Material::Clear()
	{
		m_Name.clear();
		m_TemplateName.clear();

		// options reset
		m_BlendMode = MATERIAL_BLEND_MODE_OPAQUE;
		m_CullMode = CULL_MODE_BACK;
		m_bFrontCounterClockwise = true;
		m_bDepthEnable = true;
		m_bDepthWriteEnable = true;
		m_DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		// layout cache
		m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		m_Variables.clear();
		m_ImmutableSamplersStorage.clear();

		m_CBufferBlobs.clear();
		m_TextureBindings.clear();

		m_Values.clear();
		m_Textures.clear();
	}
} // namespace shz
