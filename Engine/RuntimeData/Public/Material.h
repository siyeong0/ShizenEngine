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
	// MaterialTextureBinding: keep for runtime binding (sampler override etc.)
	// (same as your "big code" concept)
	// ---------------------------------------------------------------------
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

	// Snapshot structs can remain as-is if you still need them,
	// but you asked to keep MaterialTexture/MaterialValueBlob simple.
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
		const std::string& GetTemplateName() const noexcept { return m_TemplateName; }

		const MaterialTemplate& GetTemplate() const noexcept { return m_Template; }

		// Options
		void SetBlendMode(MATERIAL_BLEND_MODE mode);
		void SetCullMode(CULL_MODE mode);
		void SetFrontCounterClockwise(bool v);
		void SetDepthEnable(bool v);
		void SetDepthWriteEnable(bool v);
		void SetDepthFunc(COMPARISON_FUNCTION f);

		MATERIAL_BLEND_MODE GetBlendMode() const noexcept { return m_Options.BlendMode; }
		CULL_MODE GetCullMode() const noexcept { return m_Options.CullMode; }
		bool GetFrontCounterClockwise() const noexcept { return m_Options.FrontCounterClockwise; }
		bool GetDepthEnable() const noexcept { return m_Options.DepthEnable; }
		bool GetDepthWriteEnable() const noexcept { return m_Options.DepthWriteEnable; }
		COMPARISON_FUNCTION GetDepthFunc() const noexcept { return m_Options.DepthFunc; }

		SHADER_RESOURCE_VARIABLE_TYPE GetDefaultVariableType() const noexcept { return m_DefaultVariableType; }

		uint32 GetLayoutVarCount() const noexcept { return static_cast<uint32>(m_Variables.size()); }
		const ShaderResourceVariableDesc* GetLayoutVars() const noexcept { return m_Variables.empty() ? nullptr : m_Variables.data(); }

		// CBuffer blobs (used by Renderer binding)
		uint32 GetCBufferBlobCount() const noexcept { return static_cast<uint32>(m_CBufferBlobs.size()); }
		const uint8* GetCBufferBlobData(uint32 cbufferIndex) const;
		uint32 GetCBufferBlobSize(uint32 cbufferIndex) const;

		// Texture binding list (indexed by template resource index)
		uint32 GetTextureBindingCount() const noexcept { return static_cast<uint32>(m_TextureBindings.size()); }
		const MaterialTextureBinding& GetTextureBinding(uint32 index) const { return m_TextureBindings[index]; }
		MaterialTextureBinding& GetTextureBindingMutable(uint32 index) { return m_TextureBindings[index]; }

		// Simplified authoring maps (optional; keep them minimal)
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

		bool SetTextureAssetRef(const char* resourceName, MATERIAL_RESOURCE_TYPE expectedType, const AssetRef<Texture>& textureRef);
		bool SetSamplerOverridePtr(const char* resourceName, ISampler* pSampler);
		bool SetSamplerOverrideDesc(const char* resourceName, const SamplerDesc& desc);
		bool ClearSamplerOverride(const char* resourceName);

		GraphicsPipelineStateCreateInfo BuildGraphicsPipelineStateCreateInfo(IRenderPass* pRenderPass) const;
		ComputePipelineStateCreateInfo BuildComputePipelineStateCreateInfo() const;

		const std::vector<RefCntAutoPtr<IShader>>& GetShaders() const noexcept { return m_Template.GetShaders(); }
		const std::unordered_map<std::string, MaterialValueBlob>& GetAllValues() const noexcept { return m_Values; }
		const std::unordered_map<std::string, MaterialTexture>& GetAllTextures() const noexcept { return m_Textures; }

		void Clear();

	private:
		bool writeValueImmediate(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedType);
		bool setTextureImmediate(const char* name, MATERIAL_RESOURCE_TYPE expectedType, const AssetRef<Texture>& texRef);

		void rebuildAutoResourceLayout();
		void syncDescFromOptions();

	private:
		inline static const std::unordered_map<std::string, MaterialTemplate>* m_sTemplateLibrary = nullptr;

		// Metadata
		std::string m_Name = {};
		std::string m_TemplateName = {};

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

		// Multi-CB support
		std::vector<std::vector<uint8>> m_CBufferBlobs = {};
		std::vector<MaterialTextureBinding> m_TextureBindings = {};

		// Minimal authoring mirrors (optional, but you asked to keep them simple)
		std::unordered_map<std::string, MaterialValueBlob> m_Values = {};
		std::unordered_map<std::string, MaterialTexture> m_Textures = {};
	};

} // namespace shz
