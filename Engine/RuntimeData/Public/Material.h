#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
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

#include "Engine/Material/Public/MaterialTypes.h"
#include "Engine/Material/Public/MaterialTemplate.h"
#include "Engine/RuntimeData/Public/Texture.h"

namespace shz
{
	struct MaterialTextureBinding final
	{
		std::string Name = {};

		// Authoring/runtime: store texture reference
		std::optional<AssetRef<Texture>> TextureRef = {};

		// Authoring: store sampler override desc (persistent)
		bool bHasSamplerOverride = false;
		SamplerDesc SamplerOverrideDesc =
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
		};

		ISampler* pSamplerOverride = nullptr;
	};

	struct MaterialKey final
	{
		size_t Hash = 0;
		bool operator==(const MaterialKey& rhs) const noexcept { return Hash == rhs.Hash; }
		bool operator!=(const MaterialKey& rhs) const noexcept { return !(*this == rhs); }
	};

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

		static void RegisterTemplateLibrary(const std::unordered_map<std::string, MaterialTemplate>* pLibrary) { m_sTemplateLibrary = pLibrary; }

		const std::string& GetName() const noexcept { return m_Name; }
		void SetRenderPassName(const std::string& name);
		const std::string& GetTemplateName() const noexcept { return m_TemplateName; }
		const std::string& GetRenderPassName() const noexcept { return m_RenderPassName; }

		const MaterialTemplate& GetTemplate() const noexcept { return m_Template; }
		MATERIAL_PIPELINE_TYPE GetPipelineType() const noexcept { return m_Template.GetPipelineType(); }

		void SetBlendMode(MATERIAL_BLEND_MODE mode);
		void SetCullMode(CULL_MODE mode);
		void SetFrontCounterClockwise(bool v);
		void SetDepthEnable(bool v);
		void SetDepthWriteEnable(bool v);
		void SetDepthFunc(COMPARISON_FUNCTION f);
		void SetTextureBindingMode(MATERIAL_TEXTURE_BINDING_MODE mode);
		void SetLinearWrapSamplerName(const std::string& name);
		void SetLinearWrapSamplerDesc(const SamplerDesc& desc);

		MATERIAL_BLEND_MODE GetBlendMode() const noexcept { return m_Options.BlendMode; }
		CULL_MODE GetCullMode() const noexcept { return m_Options.CullMode; }
		bool GetFrontCounterClockwise() const noexcept { return m_Options.FrontCounterClockwise; }
		bool GetDepthEnable() const noexcept { return m_Options.DepthEnable; }
		bool GetDepthWriteEnable() const noexcept { return m_Options.DepthWriteEnable; }
		COMPARISON_FUNCTION GetDepthFunc() const noexcept { return m_Options.DepthFunc; }
		MATERIAL_TEXTURE_BINDING_MODE GetTextureBindingMode() const noexcept { return m_Options.TextureBindingMode; }
		const std::string& GetLinearWrapSamplerName() const noexcept { return m_Options.LinearWrapSamplerName; }
		const SamplerDesc& GetLinearWrapSamplerDesc() const noexcept { return m_Options.LinearWrapSamplerDesc; }

		SHADER_RESOURCE_VARIABLE_TYPE GetDefaultVariableType() const noexcept { return m_DefaultVariableType; }

		uint32 GetLayoutVarCount() const noexcept { return static_cast<uint32>(m_Variables.size()); }
		const ShaderResourceVariableDesc* GetLayoutVars() const noexcept { return m_Variables.empty() ? nullptr : m_Variables.data(); }

		uint32 GetValueOverrideCount() const;
		const MaterialSerializedValue& GetValueOverride(uint32 index) const;
		uint32 GetResourceBindingCount() const;
		const MaterialSerializedResource& GetResourceBinding(uint32 index) const;
		uint32 GetCBufferBlobCount() const noexcept { return static_cast<uint32>(m_CBufferBlobs.size()); }
		const uint8* GetCBufferBlobData(uint32 cbufferIndex) const;
		uint32 GetCBufferBlobSize(uint32 cbufferIndex) const;
		uint32 GetTextureBindingCount() const noexcept { return static_cast<uint32>(m_TextureBindings.size()); }
		const MaterialTextureBinding& GetTextureBinding(uint32 index) const { return m_TextureBindings[index]; }
		MaterialTextureBinding& GetTextureBindingMutable(uint32 index) { return m_TextureBindings[index]; }

		void BuildSerializedSnapshot(std::vector<MaterialSerializedValue>* outValues, std::vector<MaterialSerializedResource>* outResources) const;

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

		bool SetRaw(const char* name, MATERIAL_VALUE_TYPE type, const void* pData, uint32 byteSize);

		bool SetTextureAssetRef(const char* resourceName, MATERIAL_RESOURCE_TYPE expectedType, const AssetRef<Texture>& textureRef);
		bool SetSamplerOverridePtr(const char* resourceName, ISampler* pSampler);
		bool SetSamplerOverrideDesc(const char* resourceName, const SamplerDesc& desc);
		bool ClearSamplerOverride(const char* resourceName);

		GraphicsPipelineStateCreateInfo BuildGraphicsPipelineStateCreateInfo(const std::unordered_map<std::string, IRenderPass*>& renderPassLut) const;
		ComputePipelineStateCreateInfo BuildComputePipelineStateCreateInfo() const;

		const std::vector<RefCntAutoPtr<IShader>>& GetShaders() const noexcept { return m_Template.GetShaders(); }

		void Clear();

	private:
		bool writeValueImmediate(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedType);
		bool setTextureImmediate(const char* name, MATERIAL_RESOURCE_TYPE expectedType, const AssetRef<Texture>& texRef);

		void rebuildAutoResourceLayout();
		void syncDescFromOptions();

		void ensureSnapshotCache() const;

	private:
		inline static const std::unordered_map<std::string, MaterialTemplate>* m_sTemplateLibrary = nullptr;

		// Metadata
		std::string m_Name = {};
		std::string m_TemplateName = {};
		std::string m_RenderPassName = "GBuffer";

		MaterialOptions m_Options = {};

		// Runtime template binding
		const MaterialTemplate& m_Template;

		// Stored descs (plain types)
		PipelineStateDesc m_PipelineStateDesc = {};
		GraphicsPipelineDesc m_GraphicsPipelineDesc = {};
		std::vector<ImmutableSamplerDesc> m_ImmutableSamplersStorage = {};

		// Auto layout 
		SHADER_RESOURCE_VARIABLE_TYPE m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		std::vector<ShaderResourceVariableDesc> m_Variables = {};

		std::vector<std::vector<uint8>> m_CBufferBlobs = {};
		std::vector<MaterialTextureBinding> m_TextureBindings = {};

		// Snapshot cache // TODO : REMOVE
		mutable uint8 m_bSnapshotDirty = 1;
		mutable std::vector<MaterialSerializedValue> m_SnapshotValues = {};
		mutable std::vector<MaterialSerializedResource> m_SnapshotResources = {};
	};

} // namespace shz
