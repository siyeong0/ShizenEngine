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
			// Constant buffers
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

	void Material::SetRenderPassName(const std::string& name)
	{
		m_RenderPassName = name;
	}

	void Material::SetBlendMode(MATERIAL_BLEND_MODE mode)
	{
		if (m_Options.BlendMode == mode)
		{
			return;
		}

		m_Options.BlendMode = mode;
		syncDescFromOptions();
	}

	void Material::SetCullMode(CULL_MODE mode)
	{
		if (m_Options.CullMode == mode)
		{
			return;
		}

		m_Options.CullMode = mode;
		syncDescFromOptions();
	}

	void Material::SetFrontCounterClockwise(bool v)
	{
		if (m_Options.FrontCounterClockwise == v)
		{
			return;
		}

		m_Options.FrontCounterClockwise = v;
		syncDescFromOptions();
	}

	void Material::SetDepthEnable(bool v)
	{
		if (m_Options.DepthEnable == v)
		{
			return;
		}

		m_Options.DepthEnable = v;
		syncDescFromOptions();
	}

	void Material::SetDepthWriteEnable(bool v)
	{
		if (m_Options.DepthWriteEnable == v)
		{
			return;
		}

		m_Options.DepthWriteEnable = v;
		syncDescFromOptions();
	}

	void Material::SetDepthFunc(COMPARISON_FUNCTION f)
	{
		if (m_Options.DepthFunc == f)
		{
			return;
		}

		m_Options.DepthFunc = f;
		syncDescFromOptions();
	}

	void Material::SetTextureBindingMode(MATERIAL_TEXTURE_BINDING_MODE mode)
	{
		if (m_Options.TextureBindingMode == mode)
		{
			return;
		}

		m_Options.TextureBindingMode = mode;
		rebuildAutoResourceLayout();
	}

	void Material::SetLinearWrapSamplerName(const std::string& name)
	{
		const std::string newName = name.empty() ? "g_LinearWrapSampler" : name;
		if (m_Options.LinearWrapSamplerName == newName)
		{
			return;
		}

		m_Options.LinearWrapSamplerName = newName;

		rebuildAutoResourceLayout();
	}

	void Material::SetLinearWrapSamplerDesc(const SamplerDesc& desc)
	{
		if (std::memcmp(&m_Options.LinearWrapSamplerDesc, &desc, sizeof(SamplerDesc)) == 0)
		{
			return;
		}

		m_Options.LinearWrapSamplerDesc = desc;
		rebuildAutoResourceLayout();
	}

	uint32 Material::GetValueOverrideCount() const
	{
		ensureSnapshotCache();
		return static_cast<uint32>(m_SnapshotValues.size());
	}

	const MaterialSerializedValue& Material::GetValueOverride(uint32 index) const
	{
		ensureSnapshotCache();
		ASSERT(index < static_cast<uint32>(m_SnapshotValues.size()), "Out of bounds.");
		return m_SnapshotValues[index];
	}

	uint32 Material::GetResourceBindingCount() const
	{
		ensureSnapshotCache();
		return static_cast<uint32>(m_SnapshotResources.size());
	}

	const MaterialSerializedResource& Material::GetResourceBinding(uint32 index) const
	{
		ensureSnapshotCache();
		ASSERT(index < static_cast<uint32>(m_SnapshotResources.size()), "Out of bounds.");
		return m_SnapshotResources[index];
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

	void Material::BuildSerializedSnapshot(
		std::vector<MaterialSerializedValue>* outValues,
		std::vector<MaterialSerializedResource>* outResources) const
	{
		ASSERT(outValues, "outValues is null.");
		ASSERT(outResources, "outResources is null.");

		ensureSnapshotCache();
		*outValues = m_SnapshotValues;
		*outResources = m_SnapshotResources;
	}

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

		m_bSnapshotDirty = 1;
		return true;
	}

	bool Material::SetFloat(const char* name, float v)
	{
		return writeValueImmediate(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_FLOAT);
	}

	bool Material::SetFloat2(const char* name, const float v[2])
	{
		return writeValueImmediate(name, v, sizeof(float) * 2, MATERIAL_VALUE_TYPE_FLOAT2);
	}

	bool Material::SetFloat2(const char* name, const float2& v)
	{
		return writeValueImmediate(name, &v, sizeof(float) * 2, MATERIAL_VALUE_TYPE_FLOAT2);
	}

	bool Material::SetFloat3(const char* name, const float v[3])
	{
		return writeValueImmediate(name, v, sizeof(float) * 3, MATERIAL_VALUE_TYPE_FLOAT3);
	}

	bool Material::SetFloat3(const char* name, const float3& v)
	{
		return writeValueImmediate(name, &v, sizeof(float) * 3, MATERIAL_VALUE_TYPE_FLOAT3);
	}

	bool Material::SetFloat4(const char* name, const float v[4])
	{
		return writeValueImmediate(name, v, sizeof(float) * 4, MATERIAL_VALUE_TYPE_FLOAT4);
	}

	bool Material::SetFloat4(const char* name, const float4& v)
	{
		return writeValueImmediate(name, &v, sizeof(float) * 4, MATERIAL_VALUE_TYPE_FLOAT4);
	}

	bool Material::SetInt(const char* name, int32 v)
	{
		return writeValueImmediate(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_INT);
	}

	bool Material::SetInt2(const char* name, const int32 v[2])
	{
		return writeValueImmediate(name, v, sizeof(int32) * 2, MATERIAL_VALUE_TYPE_INT2);
	}

	bool Material::SetInt3(const char* name, const int32 v[3])
	{
		return writeValueImmediate(name, v, sizeof(int32) * 3, MATERIAL_VALUE_TYPE_INT3);
	}

	bool Material::SetInt4(const char* name, const int32 v[4])
	{
		return writeValueImmediate(name, v, sizeof(int32) * 4, MATERIAL_VALUE_TYPE_INT4);
	}

	bool Material::SetUint(const char* name, uint32 v)
	{
		return writeValueImmediate(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_UINT);
	}

	bool Material::SetUint2(const char* name, const uint32 v[2])
	{
		return writeValueImmediate(name, v, sizeof(uint32) * 2, MATERIAL_VALUE_TYPE_UINT2);
	}

	bool Material::SetUint3(const char* name, const uint32 v[3])
	{
		return writeValueImmediate(name, v, sizeof(uint32) * 3, MATERIAL_VALUE_TYPE_UINT3);
	}

	bool Material::SetUint4(const char* name, const uint32 v[4])
	{
		return writeValueImmediate(name, v, sizeof(uint32) * 4, MATERIAL_VALUE_TYPE_UINT4);
	}

	bool Material::SetFloat4x4(const char* name, const float m16[16])
	{
		return writeValueImmediate(name, m16, sizeof(float) * 16, MATERIAL_VALUE_TYPE_FLOAT4X4);
	}

	bool Material::SetRaw(const char* name, MATERIAL_VALUE_TYPE type, const void* pData, uint32 byteSize)
	{
		ASSERT(type != MATERIAL_VALUE_TYPE_UNKNOWN, "Value type is unknown. Please specify value type.");
		return writeValueImmediate(name, pData, byteSize, type);
	}

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

		m_bSnapshotDirty = 1;
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

		m_bSnapshotDirty = 1;
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

		// Defer pointer resolution to renderer/sampler cache
		tb.pSamplerOverride = nullptr;

		m_bSnapshotDirty = 1;
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

		m_bSnapshotDirty = 1;
		return true;
	}


	GraphicsPipelineStateCreateInfo Material::BuildGraphicsPipelineStateCreateInfo(
		const std::unordered_map<std::string, IRenderPass*>& renderPassLut) const
	{
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

			auto it = renderPassLut.find(m_RenderPassName);
			ASSERT(it != renderPassLut.end(), "Render pass '%s' not found in LUT.", m_RenderPassName.c_str());

			gp->pRenderPass = it->second;

			// IMPORTANT:
			// When pRenderPass != null:
			// - NumRenderTargets must be 0
			// - RTVFormats[] and DSVFormat must be UNKNOWN
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

			// classify for earlier diagnostic
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

		// Diligent/your utils assert: mesh stages can't be combined with legacy stages
		ASSERT(!(bHasMeshStages && bHasLegacyStages), "Invalid shader stage mix: mesh stages can't be combined with VS/GS/HS/DS.");

		return outGraphicsPipelineStateCI;
	}

	ComputePipelineStateCreateInfo Material::BuildComputePipelineStateCreateInfo() const
	{
		ComputePipelineStateCreateInfo outComputePipelineStateCI = {};
		PipelineStateDesc& psDesc = outComputePipelineStateCI.PSODesc;
		psDesc = m_PipelineStateDesc;
		// Attach shaders from instance
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

	// ------------------------------------------------------------
	// Reset
	// ------------------------------------------------------------

	void Material::Clear()
	{
		m_Name.clear();
		m_TemplateName.clear();
		m_RenderPassName = "GBuffer";

		m_Options = {};

		m_PipelineStateDesc = {};
		m_GraphicsPipelineDesc = {};

		m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		m_Variables.clear();
		m_ImmutableSamplersStorage.clear();

		m_CBufferBlobs.clear();
		m_TextureBindings.clear();

		m_SnapshotValues.clear();
		m_SnapshotResources.clear();
		m_bSnapshotDirty = 1;
	}

	void Material::rebuildAutoResourceLayout()
	{
		m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		m_Variables.clear();
		m_ImmutableSamplersStorage.clear();

		m_Variables.reserve(32);
		m_ImmutableSamplersStorage.reserve(4);

		// Constant buffer
		if (m_Template.GetCBufferCount() > 0)
		{
			ShaderResourceVariableDesc v = {};
			v.ShaderStages = SHADER_TYPE_PIXEL; // TODO: reflect stages
			v.Name = MaterialTemplate::MATERIAL_CBUFFER_NAME;
			v.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			m_Variables.push_back(v);
		}

		// Textures
		const SHADER_RESOURCE_VARIABLE_TYPE texVarType =
			(m_Options.TextureBindingMode == MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC)
			? SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC
			: SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

		const uint32 resCount = m_Template.GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& r = m_Template.GetResource(i);

			if (IsTextureType(r.Type))
			{
				ShaderResourceVariableDesc v = {};
				v.ShaderStages = SHADER_TYPE_PIXEL; // TODO
				v.Name = r.Name.c_str();
				v.Type = texVarType;
				m_Variables.push_back(v);
			}
		}

		// Immutable sampler: LinearWrap
		{
			ImmutableSamplerDesc s = {};
			s.ShaderStages = SHADER_TYPE_PIXEL; // TODO: Vertex samplers possible
			s.SamplerOrTextureName = m_Options.LinearWrapSamplerName.c_str();
			s.Desc = m_Options.LinearWrapSamplerDesc;
			m_ImmutableSamplersStorage.push_back(s);
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
		// Pipeline type
		{
			const MATERIAL_PIPELINE_TYPE t = m_Template.GetPipelineType();
			if (t == MATERIAL_PIPELINE_TYPE_COMPUTE)
			{
				m_PipelineStateDesc.PipelineType = PIPELINE_TYPE_COMPUTE;
			}
			else
			{
				m_PipelineStateDesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
			}
		}

		// Name policy (debug)
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

		// Graphics pipeline (only meaningful for graphics)
		if (m_PipelineStateDesc.IsAnyGraphicsPipeline())
		{
			// Policy: formats come from RenderPass => keep unknowns here.
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

			// Input layout policy: fixed mesh layout (adjust to your engine's vertex format)
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

	void Material::ensureSnapshotCache() const
	{
		if (m_bSnapshotDirty == 0)
		{
			return;
		}

		m_SnapshotValues.clear();
		m_SnapshotResources.clear();

		// Values from reflected params -> current blob bytes
		{
			const uint32 valueCount = m_Template.GetValueParamCount();
			m_SnapshotValues.reserve(valueCount);

			for (uint32 i = 0; i < valueCount; ++i)
			{
				const MaterialValueParamDesc& vp = m_Template.GetValueParam(i);

				MaterialSerializedValue v = {};
				v.Name = vp.Name;
				v.Type = vp.Type;

				ASSERT(vp.CBufferIndex < m_CBufferBlobs.size(), "Out of bounds.");
				const std::vector<uint8>& blob = m_CBufferBlobs[vp.CBufferIndex];

				const uint32 maxCopy = static_cast<uint32>(blob.size()) - vp.ByteOffset;
				const uint32 copySize = std::min<uint32>(vp.ByteSize, maxCopy);

				v.Data.resize(copySize);
				std::memcpy(v.Data.data(), blob.data() + vp.ByteOffset, copySize);

				m_SnapshotValues.push_back(static_cast<MaterialSerializedValue&&>(v));
			}
		}

		// Resources
		{
			const uint32 resCount = m_Template.GetResourceCount();
			m_SnapshotResources.reserve(resCount);

			for (uint32 i = 0; i < resCount; ++i)
			{
				const MaterialResourceDesc& rr = m_Template.GetResource(i);
				if (!IsTextureType(rr.Type))
				{
					continue;
				}

				MaterialSerializedResource r = {};
				r.Name = rr.Name;
				r.Type = rr.Type;

				if (i < m_TextureBindings.size())
				{
					const MaterialTextureBinding& tb = m_TextureBindings[i];

					if (tb.TextureRef.has_value())
					{
						r.TextureRef = tb.TextureRef.value();
					}

					r.bHasSamplerOverride = tb.bHasSamplerOverride;
					if (tb.bHasSamplerOverride)
					{
						r.SamplerOverrideDesc = tb.SamplerOverrideDesc;
					}
				}

				m_SnapshotResources.push_back(static_cast<MaterialSerializedResource&&>(r));
			}
		}

		m_bSnapshotDirty = 0;
	}
} // namespace shz
