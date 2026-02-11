#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Common/Public/HashUtils.hpp"

#include "Engine/AssetManager/Public/AssetRef.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/ISampler.h"
#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IShaderResourceVariable.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/RuntimeData/Public/MaterialTypes.h"
#include "Engine/RuntimeData/Public/MaterialTemplate.h"
#include "Engine/RuntimeData/Public/Texture.h"

namespace shz
{
	using MaterialId = uint64;

	// ---------------------------------------------------------------------
	// Simplified authoring data blobs (keep it minimal)
	// ---------------------------------------------------------------------
	struct MaterialTexture final
	{
		AssetRef<Texture> Texture;
	};

	struct MaterialValueBlob final
	{
		MATERIAL_VALUE_TYPE Type = MATERIAL_VALUE_TYPE_UNKNOWN;
		std::vector<uint8> Data = {};
	};

	// ---------------------------------------------------------------------
	// Runtime resource binding
	// - For textures: AssetRef<Texture> OR ResourceId (mutually exclusive)
	// - For buffers (SRV/UAV): ResourceId only
	// ---------------------------------------------------------------------
	enum class MATERIAL_BINDING_SOURCE : uint8
	{
		None = 0,
		AssetRef,   // only valid for textures
		ResourceId,
	};

	struct MaterialResourceBinding final
	{
		std::string Name = {};

		MATERIAL_RESOURCE_TYPE ExpectedType = MATERIAL_RESOURCE_TYPE_UNKNOWN;
		uint16 ArraySize = 1;

		MATERIAL_BINDING_SOURCE Source = MATERIAL_BINDING_SOURCE::None;

		// Texture authoring path (only if ExpectedType is texture)
		std::optional<AssetRef<Texture>> TextureRef = {};

		// Registry path for any resource (texture/buffer/uav)
		uint64 ResourceId = 0; // 0 = invalid

		void ClearBinding()
		{
			Source = MATERIAL_BINDING_SOURCE::None;
			TextureRef.reset();
			ResourceId = 0;
		}

		bool IsTextureBinding() const
		{
			return IsTextureType(ExpectedType);
		}

		bool HasAssetRef() const
		{
			return Source == MATERIAL_BINDING_SOURCE::AssetRef && TextureRef.has_value();
		}

		bool HasResourceId() const
		{
			return Source == MATERIAL_BINDING_SOURCE::ResourceId && ResourceId != 0;
		}

		void SetTextureAssetRef(const AssetRef<Texture>& ref)
		{
			ASSERT(IsTextureBinding(), "SetTextureAssetRef is only valid for texture bindings.");
			Source = MATERIAL_BINDING_SOURCE::AssetRef;
			TextureRef = ref;
			ResourceId = 0;
		}

		void SetResourceId(uint64 id)
		{
			ASSERT(id != 0, "Invalid resource id (0).");
			Source = MATERIAL_BINDING_SOURCE::ResourceId;
			TextureRef.reset();
			ResourceId = id;
		}
	};

	// Optional snapshot structs (kept as-is)
	struct MaterialSerializedValue final
	{
		std::string Name = {};
		MATERIAL_VALUE_TYPE Type = MATERIAL_VALUE_TYPE_UNKNOWN;
		std::vector<uint8> Data = {};
	};

	struct MaterialSerializedResource final
	{
		std::string Name = {};
		MATERIAL_RESOURCE_TYPE Type = MATERIAL_RESOURCE_TYPE_UNKNOWN;

		AssetRef<Texture> TextureRef = {};

		bool bHasSamplerOverride = false;
		SamplerDesc SamplerOverrideDesc =
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
		};
	};

	class Material final
	{
	public:
		Material(const std::string& name, const std::string& templateName);
		Material(const Material&) = default;
		Material(Material&&) noexcept = default;
		Material& operator=(const Material&) = default;
		Material& operator=(Material&&) noexcept = default;
		~Material() = default;

		static void RegisterTemplateLibrary(const std::unordered_map<std::string, MaterialTemplate>* pLibrary)
		{
			m_sTemplateLibrary = pLibrary;
		}

		const std::string& GetName() const noexcept { return m_Name; }

		const std::string& GetTemplateName() const noexcept { return m_BaseTemplateName; }
		const MaterialTemplate& GetTemplate() const noexcept { return m_BaseTemplate; }

		void SetBlendMode(MATERIAL_BLEND_MODE mode) { m_BlendMode = mode; }
		void SetCullMode(CULL_MODE mode) { m_CullMode = mode; }
		void SetFrontCounterClockwise(bool v) { m_bFrontCounterClockwise = v; }
		void SetDepthEnable(bool v) { m_bDepthEnable = v; }
		void SetDepthWriteEnable(bool v) { m_bDepthWriteEnable = v; }
		void SetDepthFunc(COMPARISON_FUNCTION f) { m_DepthFunc = f; }

		MATERIAL_BLEND_MODE GetBlendMode() const noexcept { return m_BlendMode; }
		CULL_MODE GetCullMode() const noexcept { return m_CullMode; }
		bool GetFrontCounterClockwise() const noexcept { return m_bFrontCounterClockwise; }
		bool GetDepthEnable() const noexcept { return m_bDepthEnable; }
		bool GetDepthWriteEnable() const noexcept { return m_bDepthWriteEnable; }
		COMPARISON_FUNCTION GetDepthFunc() const noexcept { return m_DepthFunc; }

		// Resource layout (auto-built once)
		SHADER_RESOURCE_VARIABLE_TYPE GetDefaultVariableType() const noexcept { return m_DefaultVariableType; }

		uint32 GetLayoutVarCount() const noexcept { return static_cast<uint32>(m_Variables.size()); }
		const ShaderResourceVariableDesc* GetLayoutVars() const noexcept { return m_Variables.empty() ? nullptr : m_Variables.data(); }

		uint32 GetImmutableSamplerCount() const noexcept { return static_cast<uint32>(m_ImmutableSamplersStorage.size()); }
		const ImmutableSamplerDesc* GetImmutableSamplers() const noexcept { return m_ImmutableSamplersStorage.empty() ? nullptr : m_ImmutableSamplersStorage.data(); }

		// CBuffer blobs
		uint32 GetCBufferBlobCount() const noexcept { return static_cast<uint32>(m_CBufferBlobs.size()); }
		const uint8* GetCBufferBlobData(uint32 cbufferIndex) const;
		uint32 GetCBufferBlobSize(uint32 cbufferIndex) const;

		// Runtime resource bindings (indexed by template resource index)
		uint32 GetResourceBindingCount() const noexcept { return static_cast<uint32>(m_ResourceBindings.size()); }
		const MaterialResourceBinding& GetResourceBinding(uint32 index) const { return m_ResourceBindings[index]; }
		MaterialResourceBinding& GetResourceBindingMutable(uint32 index) { return m_ResourceBindings[index]; }

		// Simplified authoring mirrors (texture only)
		const MaterialValueBlob* GetValueOrNull(const std::string& name) const noexcept;
		const MaterialTexture* GetTextureOrNull(const std::string& name) const noexcept;

		// Write APIs
		bool SetFloat(const char* name, float v);
		bool SetFloat2(const char* name, const float v[2]);
		bool SetFloat2(const char* name, const float2& v);
		bool SetFloat3(const char* name, const float v[3]);
		bool SetFloat3(const char* name, const float3& v);
		bool SetFloat4(const char* name, const float v[4]);
		bool SetFloat4(const char* name, const float4& v);

		bool SetInt(const char* name, int32 v);
		bool SetInt2(const char* name, const int32 v[2]);
		bool SetInt3(const char* name, const int32 v[3]);
		bool SetInt4(const char* name, const int32 v[4]);

		bool SetUint(const char* name, uint32 v);
		bool SetUint2(const char* name, const uint32 v[2]);
		bool SetUint3(const char* name, const uint32 v[3]);
		bool SetUint4(const char* name, const uint32 v[4]);

		bool SetFloat4x4(const char* name, const float m16[16]);

		bool SetRaw(const char* name, MATERIAL_VALUE_TYPE type, const void* pData, uint32 byteSize);

		// Resource binding
		// - For textures, you can bind by AssetRef or ResourceId
		bool SetTextureAssetRef(const char* resourceName, const AssetRef<Texture>& textureRef);
		bool SetTextureResource(const char* resourceName, uint64 resourceId);

		// - For buffers (StructuredBuffer / RWStructuredBuffer), bind by ResourceId
		bool SetBufferResource(const char* resourceName, uint64 resourceId);

		GraphicsPipelineStateCreateInfo BuildGraphicsPipelineStateCreateInfo(IRenderPass* pRenderPass, EMaterialPass pass) const;
		const std::vector<RefCntAutoPtr<IShader>>& GetShaders(EMaterialPass pass) const noexcept;

		const std::unordered_map<std::string, MaterialValueBlob>& GetAllValues() const noexcept { return m_Values; }
		const std::unordered_map<std::string, MaterialTexture>& GetAllTextures() const noexcept { return m_Textures; }

		void Clear();

	private:
		bool writeValueImmediate(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedType);

		bool setResourceIdInternal(const char* resourceName, uint64 resourceId, bool bRequireTexture, bool bRequireBuffer);

	private:
		inline static const std::unordered_map<std::string, MaterialTemplate>* m_sTemplateLibrary = nullptr;

		// Metadata
		std::string m_Name = {};

		std::string m_BaseTemplateName = {};
		const MaterialTemplate& m_BaseTemplate;

		std::string m_DepthOnlyTemplateName = {};
		const MaterialTemplate& m_DepthOnlyTemplate;

		// Minimal options
		MATERIAL_BLEND_MODE m_BlendMode = MATERIAL_BLEND_MODE_OPAQUE;

		// Raster
		CULL_MODE m_CullMode = CULL_MODE_BACK;
		bool m_bFrontCounterClockwise = true;

		// Depth
		bool m_bDepthEnable = true;
		bool m_bDepthWriteEnable = true;
		COMPARISON_FUNCTION m_DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		// Auto layout cache (built once)
		SHADER_RESOURCE_VARIABLE_TYPE m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		std::vector<ShaderResourceVariableDesc> m_Variables = {};
		std::vector<ImmutableSamplerDesc> m_ImmutableSamplersStorage = {};

		// Multi-CB support
		std::vector<std::vector<uint8>> m_CBufferBlobs = {};

		// One binding per template resource (textures + buffers)
		std::vector<MaterialResourceBinding> m_ResourceBindings = {};

		// Minimal authoring mirrors
		std::unordered_map<std::string, MaterialValueBlob> m_Values = {};
		std::unordered_map<std::string, MaterialTexture> m_Textures = {};
	};

} // namespace shz
