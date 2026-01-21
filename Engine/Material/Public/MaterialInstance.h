#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Common/AssetRef.hpp"
#include "Engine/AssetRuntime/AssetData/Public/TextureAsset.h"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/ISampler.h"
#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IShaderResourceVariable.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Material/Public/MaterialTypes.h"
#include "Engine/Material/Public/MaterialTemplate.h"

namespace shz
{
	struct TextureBinding final
	{
		std::string Name = {};
		std::optional<AssetRef<TextureAsset>> TextureRef = {};
		ISampler* pSamplerOverride = nullptr;
	};

	class MaterialInstance final
	{
	public:
		MaterialInstance() = default;
		~MaterialInstance() = default;

		MaterialInstance(const MaterialInstance&) = delete;
		MaterialInstance& operator=(const MaterialInstance&) = delete;

		MaterialInstance(MaterialInstance&&) noexcept = default;
		MaterialInstance& operator=(MaterialInstance&&) noexcept = default;

		// NOTE:
		// - All pipeline/binding knobs are set by setter APIs (and mark dirty bits).
		bool Initialize(const MaterialTemplate* pTemplate, const std::string& instanceName);

		MATERIAL_PIPELINE_TYPE GetPipelineType() const { return m_pTemplate ? m_pTemplate->GetPipelineType() : MATERIAL_PIPELINE_TYPE_UNKNOWN; }
		const MaterialTemplate* GetTemplate() const { return m_pTemplate; }

		uint32 GetShaderCount() const { return m_pTemplate ? m_pTemplate->GetShaderCount() : 0; }
		IShader* GetShader(uint32 index) const { return m_pTemplate ? m_pTemplate->GetShader(index) : nullptr; }
		const std::vector<RefCntAutoPtr<IShader>>& GetShaders() const { return m_pTemplate->GetShaders(); }

		const PipelineStateDesc& GetPSODesc() const { return m_PSODesc; }
		const GraphicsPipelineDesc& GetGraphicsPipelineDesc() const { return m_GraphicsPipeline; }
		PipelineStateDesc& GetPSODesc() { return m_PSODesc; }
		GraphicsPipelineDesc& GetGraphicsPipelineDesc() { return m_GraphicsPipeline; }

		// RenderPass policy:
		// - RenderPass determines formats. So NumRenderTargets=0 and formats are UNKNOWN.
		// - RenderPass can be null in editor; in that case PSO creation must be deferred.
		void SetRenderPass(const std::string& renderPassName);
		void SetBlendMode(MATERIAL_BLEND_MODE mode);

		// Raster / depth knobs
		void SetCullMode(CULL_MODE mode);
		void SetFrontCounterClockwise(bool v);

		void SetDepthEnable(bool v);
		void SetDepthWriteEnable(bool v);
		void SetDepthFunc(COMPARISON_FUNCTION func);

		// Resource binding policy for textures
		void SetTextureBindingMode(MATERIAL_TEXTURE_BINDING_MODE mode);

		// Fixed immutable sampler (global 1)
		void SetLinearWrapSamplerName(const std::string& name);
		void SetLinearWrapSamplerDesc(const SamplerDesc& desc);

		const std::string& GetRenderPass() const noexcept { return m_RenderPassName; }
		MATERIAL_BLEND_MODE GetBlendMode() const noexcept { return m_Options.BlendMode; }

		CULL_MODE GetCullMode() const noexcept { return m_Options.CullMode; }
		bool GetFrontCounterClockwise() const noexcept { return m_Options.FrontCounterClockwise; }

		bool GetDepthEnable() const noexcept { return m_Options.DepthEnable; }
		bool GetDepthWriteEnable() const noexcept { return m_Options.DepthWriteEnable; }
		COMPARISON_FUNCTION GetDepthFunc() const noexcept { return m_Options.DepthFunc; }

		MATERIAL_TEXTURE_BINDING_MODE GetTextureBindingMode() const noexcept { return m_Options.TextureBindingMode; }

		const char* GetLinearWrapSamplerName() const noexcept { return m_Options.LinearWrapSamplerName.c_str(); }
		const SamplerDesc& GetLinearWrapSamplerDesc() const noexcept { return m_Options.LinearWrapSamplerDesc; }

		// Resource layout (auto-generated from template reflection)
		SHADER_RESOURCE_VARIABLE_TYPE GetDefaultVariableType() const { return m_DefaultVariableType; }

		uint32 GetLayoutVarCount() const { return static_cast<uint32>(m_Variables.size()); }
		const ShaderResourceVariableDesc* GetLayoutVars() const { return m_Variables.empty() ? nullptr : m_Variables.data(); }

		uint32 GetImmutableSamplerCount() const { return static_cast<uint32>(m_ImmutableSamplers.size()); }
		const ImmutableSamplerDesc* GetImmutableSamplers() const { return m_ImmutableSamplers.empty() ? nullptr : m_ImmutableSamplers.data(); }

		// Dirty for MaterialRenderData (PSO / SRB rebuild triggers)
		bool IsPsoDirty() const { return m_bPsoDirty != 0; }
		void ClearPsoDirty() { m_bPsoDirty = 0; }

		bool IsLayoutDirty() const { return m_bLayoutDirty != 0; }
		void ClearLayoutDirty() { m_bLayoutDirty = 0; }

		// --------------------------------------------------------------------
		// Values
		// --------------------------------------------------------------------
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

		bool SetValue(const char* name, const void* pData, MATERIAL_VALUE_TYPE valType);

		// --------------------------------------------------------------------
		// Resources
		// --------------------------------------------------------------------
		bool SetTextureAssetRef(const char* textureName, const AssetRef<TextureAsset>& textureRef);
		bool SetSamplerOverride(const char* textureName, ISampler* pSampler);

		bool ClearTextureAssetRef(const char* textureName);
		bool ClearSamplerOverride(const char* textureName);

		// --------------------------------------------------------------------
		// For MaterialRenderData
		// --------------------------------------------------------------------
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

		void buildAutoResourceLayout();

		void markPsoDirty() { m_bPsoDirty = 1; }
		void markLayoutDirty() { m_bLayoutDirty = 1; }

	private:
		std::string m_InstanceName = {};
		const MaterialTemplate* m_pTemplate = nullptr;

		// Shared knobs (also used by MaterialAsset Options)
		MaterialCommonOptions m_Options = {};

		// Pipeline state owned by instance
		PipelineStateDesc m_PSODesc = {};
		GraphicsPipelineDesc m_GraphicsPipeline = {};

		// RenderPass selection
		std::string m_RenderPassName = "GBuffer";

		// Auto layout
		SHADER_RESOURCE_VARIABLE_TYPE m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		std::vector<ShaderResourceVariableDesc> m_Variables = {};
		std::vector<ImmutableSamplerDesc> m_ImmutableSamplers = {};

		// Constant buffers (CPU-side blobs)
		std::vector<std::vector<uint8>> m_CBufferBlobs = {};
		std::vector<uint8> m_bCBufferDirties = {};

		// Resources
		std::vector<TextureBinding> m_TextureBindings = {};
		std::vector<uint8> m_bTextureDirties = {};

		// PSO/Layout rebuild triggers
		uint8 m_bPsoDirty = 1;
		uint8 m_bLayoutDirty = 1;
	};

} // namespace shz
