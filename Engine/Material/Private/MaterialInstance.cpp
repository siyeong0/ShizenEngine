#include "pch.h"
#include "Engine/Material/Public/MaterialInstance.h"
#include "Engine/Core/Common/Public/HashUtils.hpp"

namespace shz
{
	static inline uint64 ptrKey64(const void* p) noexcept
	{
		return static_cast<uint64>(reinterpret_cast<uintptr_t>(p));
	}

	MaterialInstanceKey MaterialInstance::ComputeKey(bool bCastShadow, bool bAlphaMasked) const
	{
		DefaultHasher Hasher;

		// ------------------------------------------------------------
		// Template identity (TODO: replace with Template AssetID/StableID)
		// ------------------------------------------------------------
		Hasher(ptrKey64(m_pTemplate));

		// Render pass name
		Hasher(HashMapStringKey{ m_RenderPassName.c_str(), false });

		// Pipeline knobs (PSO 영향)
		Hasher(
			m_Options.BlendMode,
			m_Options.CullMode,
			m_Options.FrontCounterClockwise,
			m_Options.DepthEnable,
			m_Options.DepthWriteEnable,
			m_Options.DepthFunc,
			m_Options.TextureBindingMode
		);

		// immutable sampler policy (네 Options에 있는 것들)
		Hasher(HashMapStringKey{ m_Options.LinearWrapSamplerName.c_str(), false });
		Hasher(m_Options.LinearWrapSamplerDesc); // 네 HashCombiner<SamplerDesc>가 이미 “필드별”로 안전하게 처리함

		// Shadow/Masked variant flags
		Hasher(bCastShadow, bAlphaMasked);

		// ------------------------------------------------------------
		// CBuffer blobs (content 기반)
		// ------------------------------------------------------------
		const uint32 cbCount = GetCBufferBlobCount();
		Hasher(cbCount);

		for (uint32 i = 0; i < cbCount; ++i)
		{
			const uint8* p = GetCBufferBlobData(i);
			const uint32 sz = GetCBufferBlobSize(i);

			Hasher(i, sz);
			if (p != nullptr && sz != 0)
			{
				Hasher.UpdateRaw(p, sz);
			}
		}

		// ------------------------------------------------------------
		// Texture bindings (resource identity)
		// ------------------------------------------------------------
		const uint32 texCount = GetTextureBindingCount();
		Hasher(texCount);

		for (uint32 i = 0; i < texCount; ++i)
		{
			const TextureBinding& tb = GetTextureBinding(i);

			Hasher(i);
			Hasher(HashMapStringKey{ tb.Name.c_str(), false });

			if (tb.TextureRef.has_value() && tb.TextureRef.value())
			{
				// AssetID는 std::hash<AssetID>가 네 프로젝트에 이미 정의돼 있으니 그대로 사용 가능
				Hasher(tb.TextureRef.value().GetID());
			}
			else
			{
				Hasher(uint64{ 0 });
			}

			// Sampler override identity (추후 desc 기반으로 개선 가능)
			Hasher(ptrKey64(tb.pSamplerOverride));
		}

		return MaterialInstanceKey{ Hasher.Get() };
	}

	bool MaterialInstance::Initialize(const MaterialTemplate* pTemplate, const std::string& instanceName)
	{
		ASSERT(pTemplate, "Template is null.");
		ASSERT(pTemplate->GetPipelineType() != MATERIAL_PIPELINE_TYPE_UNKNOWN, "Invalid pipeline type.");

		m_pTemplate = pTemplate;
		m_InstanceName = instanceName;

		m_Variables.clear();
		m_ImmutableSamplers.clear();
		m_CBufferBlobs.clear();
		m_bCBufferDirties.clear();
		m_TextureBindings.clear();
		m_bTextureDirties.clear();

		m_Options = {};

		// ------------------------------------------------------------
		// PSO desc (RenderPass-driven formats policy)
		// ------------------------------------------------------------
		m_PSODesc = {};
		m_GraphicsPipeline = {};

		if (m_pTemplate->GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			m_PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

			// Policy: formats come from RenderPass/subpass.
			m_GraphicsPipeline.NumRenderTargets = 0;
			for (uint32 i = 0; i < _countof(m_GraphicsPipeline.RTVFormats); ++i)
				m_GraphicsPipeline.RTVFormats[i] = TEX_FORMAT_UNKNOWN;
			m_GraphicsPipeline.DSVFormat = TEX_FORMAT_UNKNOWN;

			m_GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			m_GraphicsPipeline.RasterizerDesc.CullMode = m_Options.CullMode;
			m_GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = m_Options.FrontCounterClockwise;

			m_GraphicsPipeline.DepthStencilDesc.DepthEnable = m_Options.DepthEnable;
			m_GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = m_Options.DepthWriteEnable;
			m_GraphicsPipeline.DepthStencilDesc.DepthFunc = m_Options.DepthFunc;

			// Build fixed layout
			static LayoutElement kLayoutElems[] =
			{
				LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
				LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
				LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
				LayoutElement{3, 0, 3, VT_FLOAT32, false}, // Tangent
			};

			m_GraphicsPipeline.InputLayout.LayoutElements = kLayoutElems;
			m_GraphicsPipeline.InputLayout.NumElements = _countof(kLayoutElems);
		}
		else if (m_pTemplate->GetPipelineType() == MATERIAL_PIPELINE_TYPE_COMPUTE)
		{
			m_PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
		}
		else
		{
			ASSERT(false, "Unsupported pipeline type.");
			return false;
		}

		// Debug name
		{
			if (!m_InstanceName.empty())
			{
				m_PSODesc.Name = m_InstanceName.c_str();
			}
			else if (!m_pTemplate->GetName().empty())
			{
				m_PSODesc.Name = m_pTemplate->GetName().c_str();
			}
			else
			{
				m_PSODesc.Name = "Material PSO";
			}
		}

		// Auto resource layout from template
		buildAutoResourceLayout();

		// Allocate CB blobs
		const uint32 cbCount = m_pTemplate->GetCBufferCount();
		m_CBufferBlobs.resize(cbCount);
		m_bCBufferDirties.resize(cbCount);

		for (uint32 i = 0; i < cbCount; ++i)
		{
			const auto& CB = m_pTemplate->GetCBuffer(i);

			m_CBufferBlobs[i].resize(CB.ByteSize);
			std::memset(m_CBufferBlobs[i].data(), 0, CB.ByteSize);

			m_bCBufferDirties[i] = 1;
		}

		// Allocate resource bindings aligned with template resources
		const uint32 resCount = m_pTemplate->GetResourceCount();
		m_TextureBindings.resize(resCount);
		m_bTextureDirties.resize(resCount);

		for (uint32 i = 0; i < resCount; ++i)
		{
			m_TextureBindings[i] = {};
			m_bTextureDirties[i] = 1;
		}

		m_bPsoDirty = 1;
		m_bLayoutDirty = 1;

		MarkAllDirty();
		return true;
	}

	// --------------------------------------------------------------------
	// Setters (mark dirty)
	// --------------------------------------------------------------------

	void MaterialInstance::SetRenderPass(const std::string& renderPassName)
	{
		m_RenderPassName = renderPassName;
	}

	void MaterialInstance::SetBlendMode(MATERIAL_BLEND_MODE mode)
	{
		if (m_Options.BlendMode == mode)
		{
			return;
		}

		m_Options.BlendMode = mode;
	}

	void MaterialInstance::SetCullMode(CULL_MODE mode)
	{
		if (m_Options.CullMode == mode)
		{
			return;
		}

		m_Options.CullMode = mode;

		if (m_pTemplate && m_pTemplate->GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			m_GraphicsPipeline.RasterizerDesc.CullMode = m_Options.CullMode;
			markPsoDirty();
		}
	}

	void MaterialInstance::SetFrontCounterClockwise(bool v)
	{
		if (m_Options.FrontCounterClockwise == v)
		{
			return;
		}

		m_Options.FrontCounterClockwise = v;

		if (m_pTemplate && m_pTemplate->GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			m_GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = m_Options.FrontCounterClockwise;
			markPsoDirty();
		}
	}

	void MaterialInstance::SetDepthEnable(bool v)
	{
		if (m_Options.DepthEnable == v)
		{
			return;
		}

		m_Options.DepthEnable = v;

		if (m_pTemplate && m_pTemplate->GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			m_GraphicsPipeline.DepthStencilDesc.DepthEnable = m_Options.DepthEnable;
			markPsoDirty();
		}
	}

	void MaterialInstance::SetDepthWriteEnable(bool v)
	{
		if (m_Options.DepthWriteEnable == v)
		{
			return;
		}

		m_Options.DepthWriteEnable = v;

		if (m_pTemplate && m_pTemplate->GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			m_GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = m_Options.DepthWriteEnable;
			markPsoDirty();
		}
	}

	void MaterialInstance::SetDepthFunc(COMPARISON_FUNCTION func)
	{
		if (m_Options.DepthFunc == func)
		{
			return;
		}

		m_Options.DepthFunc = func;

		if (m_pTemplate && m_pTemplate->GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			m_GraphicsPipeline.DepthStencilDesc.DepthFunc = m_Options.DepthFunc;
			markPsoDirty();
		}
	}

	void MaterialInstance::SetTextureBindingMode(MATERIAL_TEXTURE_BINDING_MODE mode)
	{
		if (m_Options.TextureBindingMode == mode)
		{
			return;
		}

		m_Options.TextureBindingMode = mode;
		buildAutoResourceLayout();
		markLayoutDirty();
	}

	void MaterialInstance::SetLinearWrapSamplerName(const std::string& name)
	{
		const std::string newName = name.empty() ? "g_LinearWrapSampler" : name;
		if (m_Options.LinearWrapSamplerName == newName)
		{
			return;
		}

		m_Options.LinearWrapSamplerName = newName;
		buildAutoResourceLayout();
		markLayoutDirty();
	}

	void MaterialInstance::SetLinearWrapSamplerDesc(const SamplerDesc& desc)
	{
		if (std::memcmp(&m_Options.LinearWrapSamplerDesc, &desc, sizeof(SamplerDesc)) == 0)
		{
			return;
		}

		m_Options.LinearWrapSamplerDesc = desc;
		buildAutoResourceLayout();
		markLayoutDirty();
	}

	// --------------------------------------------------------------------
	// Auto resource layout
	// --------------------------------------------------------------------

	void MaterialInstance::buildAutoResourceLayout()
	{
		ASSERT(m_pTemplate, "Template is null.");

		m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		m_Variables.clear();
		m_ImmutableSamplers.clear();

		m_Variables.reserve(32);
		m_ImmutableSamplers.reserve(4);

		// Constant buffer (dynamic if exists)
		if (m_pTemplate->GetCBufferCount() > 0)
		{
			ShaderResourceVariableDesc v = {};
			v.ShaderStages = SHADER_TYPE_PIXEL; // TODO: Vertex may require material constants depending on shader.
			v.Name = MaterialTemplate::MATERIAL_CBUFFER_NAME;
			v.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			m_Variables.push_back(v);
		}

		// Textures
		const SHADER_RESOURCE_VARIABLE_TYPE texVarType =
			(m_Options.TextureBindingMode == MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC)
			? SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC
			: SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

		const uint32 resCount = m_pTemplate->GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& r = m_pTemplate->GetResource(i);

			if (IsTextureType(r.Type))
			{
				ShaderResourceVariableDesc v = {};
				v.ShaderStages = SHADER_TYPE_PIXEL; // TODO: Vertex textures possible (e.g. VT, skinning, etc.)
				v.Name = r.Name.c_str();
				v.Type = texVarType;
				m_Variables.push_back(v);
			}
		}

		// Fixed immutable sampler: LinearWrap
		{
			ImmutableSamplerDesc s = {};
			s.ShaderStages = SHADER_TYPE_PIXEL; // TODO: Vertex samplers possible
			s.SamplerOrTextureName = m_Options.LinearWrapSamplerName.c_str();
			s.Desc = m_Options.LinearWrapSamplerDesc;
			m_ImmutableSamplers.push_back(s);
		}
	}

	// --------------------------------------------------------------------
	// CBuffer / dirty helpers
	// --------------------------------------------------------------------

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
		m_bPsoDirty = true;
		for (uint8& b : m_bCBufferDirties)
		{
			b = 1; // True
		}

		for (uint8& b : m_bTextureDirties)
		{
			b = 1; // True
		}
	}

	// --------------------------------------------------------------------
	// Values
	// --------------------------------------------------------------------

	bool MaterialInstance::writeValueInternal(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedValueType)
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");
		ASSERT(pData, "pData is null.");
		ASSERT(m_pTemplate, "Template is null.");

		MaterialValueParamDesc desc = {};
		if (!m_pTemplate->ValidateSetValue(name, expectedValueType, &desc))
		{
			ASSERTION_FAILED("There is no value named %s in shader template %s.", name, m_pTemplate->GetName().c_str());
			return false;
		}

		ASSERT(desc.CBufferIndex < static_cast<uint32>(m_CBufferBlobs.size()), "Out of bounds.");
		ASSERT(byteSize > 0, "Byte size must be > 0.");
		ASSERT(byteSize <= desc.ByteSize, "Byte size must be <= variable size (%u).", desc.ByteSize);

		std::vector<uint8>& blob = m_CBufferBlobs[desc.CBufferIndex];
		const uint32 endOffset = desc.ByteOffset + byteSize;

		ASSERT(endOffset <= static_cast<uint32>(blob.size()), "Out of bounds.");

		std::memcpy(blob.data() + desc.ByteOffset, pData, byteSize);
		m_bCBufferDirties[desc.CBufferIndex] = 1;

		return true;
	}

	bool MaterialInstance::SetFloat(const char* name, float v) { return writeValueInternal(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_FLOAT); }
	bool MaterialInstance::SetFloat2(const char* name, const float v[2]) { return writeValueInternal(name, v, sizeof(float) * 2, MATERIAL_VALUE_TYPE_FLOAT2); }
	bool MaterialInstance::SetFloat3(const char* name, const float v[3]) { return writeValueInternal(name, v, sizeof(float) * 3, MATERIAL_VALUE_TYPE_FLOAT3); }
	bool MaterialInstance::SetFloat4(const char* name, const float v[4]) { return writeValueInternal(name, v, sizeof(float) * 4, MATERIAL_VALUE_TYPE_FLOAT4); }

	bool MaterialInstance::SetInt(const char* name, int32 v) { return writeValueInternal(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_INT); }
	bool MaterialInstance::SetInt2(const char* name, const int32 v[2]) { return writeValueInternal(name, v, sizeof(int32) * 2, MATERIAL_VALUE_TYPE_INT2); }
	bool MaterialInstance::SetInt3(const char* name, const int32 v[3]) { return writeValueInternal(name, v, sizeof(int32) * 3, MATERIAL_VALUE_TYPE_INT3); }
	bool MaterialInstance::SetInt4(const char* name, const int32 v[4]) { return writeValueInternal(name, v, sizeof(int32) * 4, MATERIAL_VALUE_TYPE_INT4); }

	bool MaterialInstance::SetUint(const char* name, uint32 v) { return writeValueInternal(name, &v, sizeof(v), MATERIAL_VALUE_TYPE_UINT); }
	bool MaterialInstance::SetUint2(const char* name, const uint32 v[2]) { return writeValueInternal(name, v, sizeof(uint32) * 2, MATERIAL_VALUE_TYPE_UINT2); }
	bool MaterialInstance::SetUint3(const char* name, const uint32 v[3]) { return writeValueInternal(name, v, sizeof(uint32) * 3, MATERIAL_VALUE_TYPE_UINT3); }
	bool MaterialInstance::SetUint4(const char* name, const uint32 v[4]) { return writeValueInternal(name, v, sizeof(uint32) * 4, MATERIAL_VALUE_TYPE_UINT4); }

	bool MaterialInstance::SetFloat4x4(const char* name, const float m16[16]) { return writeValueInternal(name, m16, sizeof(float) * 16, MATERIAL_VALUE_TYPE_FLOAT4X4); }

	bool MaterialInstance::SetRaw(const char* name, const void* pData, uint32 byteSize) { return writeValueInternal(name, pData, byteSize, MATERIAL_VALUE_TYPE_UNKNOWN); }

	bool MaterialInstance::SetValue(const char* name, const void* pData, MATERIAL_VALUE_TYPE valType)
	{
		ASSERT(name && name[0] != '\0', "Invalid name.");
		ASSERT(pData, "pData is null.");

		auto byteSizeOf = [](MATERIAL_VALUE_TYPE t) -> uint32
		{
			switch (t)
			{
			case MATERIAL_VALUE_TYPE_FLOAT:     return sizeof(float);
			case MATERIAL_VALUE_TYPE_FLOAT2:    return sizeof(float) * 2;
			case MATERIAL_VALUE_TYPE_FLOAT3:    return sizeof(float) * 3;
			case MATERIAL_VALUE_TYPE_FLOAT4:    return sizeof(float) * 4;

			case MATERIAL_VALUE_TYPE_INT:       return sizeof(int32);
			case MATERIAL_VALUE_TYPE_INT2:      return sizeof(int32) * 2;
			case MATERIAL_VALUE_TYPE_INT3:      return sizeof(int32) * 3;
			case MATERIAL_VALUE_TYPE_INT4:      return sizeof(int32) * 4;

			case MATERIAL_VALUE_TYPE_UINT:      return sizeof(uint32);
			case MATERIAL_VALUE_TYPE_UINT2:     return sizeof(uint32) * 2;
			case MATERIAL_VALUE_TYPE_UINT3:     return sizeof(uint32) * 3;
			case MATERIAL_VALUE_TYPE_UINT4:     return sizeof(uint32) * 4;

			case MATERIAL_VALUE_TYPE_FLOAT4X4:  return sizeof(float) * 16;

			case MATERIAL_VALUE_TYPE_UNKNOWN:
			default:
				return 0;
			}
		};

		const uint32 byteSize = byteSizeOf(valType);
		if (byteSize == 0)
		{
			ASSERTION_FAILED("SetValue do nat support UNKOWN value type. Use SetRaw");
			return false;
		}

		return writeValueInternal(name, pData, byteSize, valType);
	}


	// --------------------------------------------------------------------
	// Resources
	// --------------------------------------------------------------------

	bool MaterialInstance::SetTextureAsset(const char* textureName, const AssetRef<Texture>& textureRef)
	{
		ASSERT(textureName && textureName[0] != '\0', "Invalid name.");
		ASSERT(m_pTemplate, "Template is null.");

		uint32 resIndex = 0;
		if (!m_pTemplate->FindResourceIndex(textureName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& resourceDesc = m_pTemplate->GetResource(resIndex);
		if (!IsTextureType(resourceDesc.Type))
		{
			return false;
		}

		m_TextureBindings[resIndex].Name = textureName;
		m_TextureBindings[resIndex].TextureRef = textureRef;

		m_bTextureDirties[resIndex] = 1;
		return true;
	}

	bool MaterialInstance::SetSamplerOverride(const char* textureName, ISampler* pSampler)
	{
		ASSERT(textureName && textureName[0] != '\0', "Invalid name.");
		ASSERT(m_pTemplate, "Template is null.");

		uint32 resIndex = 0;
		if (!m_pTemplate->FindResourceIndex(textureName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& resourceDesc = m_pTemplate->GetResource(resIndex);
		if (!IsTextureType(resourceDesc.Type))
		{
			return false;
		}

		m_TextureBindings[resIndex].Name = textureName;
		m_TextureBindings[resIndex].pSamplerOverride = pSampler;

		m_bTextureDirties[resIndex] = 1;
		return true;
	}

	bool MaterialInstance::ClearTextureAsset(const char* textureName)
	{
		ASSERT(textureName && textureName[0] != '\0', "Invalid name.");
		ASSERT(m_pTemplate, "Template is null.");

		uint32 resIndex = 0;
		if (!m_pTemplate->FindResourceIndex(textureName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& resourceDesc = m_pTemplate->GetResource(resIndex);
		if (!IsTextureType(resourceDesc.Type))
		{
			return false;
		}

		m_TextureBindings[resIndex].TextureRef = {};
		m_TextureBindings[resIndex].Name = {};
		m_TextureBindings[resIndex].pSamplerOverride = nullptr;

		m_bTextureDirties[resIndex] = 1;
		return true;
	}

	bool MaterialInstance::ClearSamplerOverride(const char* textureName)
	{
		ASSERT(textureName && textureName[0] != '\0', "Invalid name.");
		ASSERT(m_pTemplate, "Template is null.");

		uint32 resIndex = 0;
		if (!m_pTemplate->FindResourceIndex(textureName, &resIndex))
		{
			return false;
		}

		const MaterialResourceDesc& resourceDesc = m_pTemplate->GetResource(resIndex);
		if (!IsTextureType(resourceDesc.Type))
		{
			return false;
		}

		m_TextureBindings[resIndex].Name = textureName;
		m_TextureBindings[resIndex].pSamplerOverride = {};

		m_bTextureDirties[resIndex] = 1;
		return true;
	}

} // namespace shz
