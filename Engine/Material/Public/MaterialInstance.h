#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Public/AssetRef.hpp"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/ISampler.h"
#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IShaderResourceVariable.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

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

	enum MATERIAL_TEXTURE_BINDING_MODE : uint8
	{
		MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC = 0,
		MATERIAL_TEXTURE_BINDING_MODE_MUTABLE,
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
		// - Initialize only takes minimal inputs.
		// - All pipeline/binding knobs are set by setter APIs (and mark dirty bits).
		bool Initialize(const MaterialTemplate* pTemplate, const std::string& instanceName);

		MATERIAL_PIPELINE_TYPE GetPipelineType() const { return m_pTemplate ? m_pTemplate->GetPipelineType() : MATERIAL_PIPELINE_TYPE_UNKNOWN; }
		const MaterialTemplate* GetTemplate() const { return m_pTemplate; }

		uint32 GetShaderCount() const { return m_pTemplate ? m_pTemplate->GetShaderCount() : 0; }
		IShader* GetShader(uint32 index) const { return m_pTemplate ? m_pTemplate->GetShader(index) : nullptr; }
		const std::vector<RefCntAutoPtr<IShader>>& GetShaders() const;

		// --------------------------------------------------------------------
		// PSO / Pipeline state (owned by instance)
		// --------------------------------------------------------------------
		const PipelineStateDesc& GetPSODesc() const { return m_PSODesc; }
		const GraphicsPipelineDesc& GetGraphicsPipelineDesc() const { return m_GraphicsPipeline; }

		// RenderPass policy:
		// - RenderPass determines formats. So NumRenderTargets=0 and formats are UNKNOWN.
		// - RenderPass can be null in editor; in that case PSO creation must be deferred.
		void SetRenderPass(IRenderPass* pRenderPass, uint32 subpassIndex);

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

		const SamplerDesc& GetLinearWrapSamplerDesc() const { return m_LinearWrapSamplerDesc; }
		const char* GetLinearWrapSamplerName() const { return m_LinearWrapSamplerName.c_str(); }

		// --------------------------------------------------------------------
		// Resource layout (auto-generated from template reflection)
		// --------------------------------------------------------------------
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

		// --------------------------------------------------------------------
		// Resources
		// --------------------------------------------------------------------
		bool SetTextureAssetRef(const char* textureName, const AssetRef<TextureAsset>& textureRef);
		bool SetTextureRuntimeView(const char* textureName, ITextureView* pView);
		bool SetSamplerOverride(const char* textureName, ISampler* pSampler);

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
		void buildFixedInputLayout();

		void markPsoDirty() { m_bPsoDirty = 1; }
		void markLayoutDirty() { m_bLayoutDirty = 1; }

		static inline bool isTextureType(MATERIAL_RESOURCE_TYPE t)
		{
			return (t == MATERIAL_RESOURCE_TYPE_TEXTURE2D) ||
				(t == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ||
				(t == MATERIAL_RESOURCE_TYPE_TEXTURECUBE);
		}

	private:
		std::string m_InstanceName = {};
		const MaterialTemplate* m_pTemplate = nullptr;

		// Pipeline state owned by instance
		PipelineStateDesc m_PSODesc = {};
		GraphicsPipelineDesc m_GraphicsPipeline = {};

		// Current state knobs
		IRenderPass* m_pRenderPass = nullptr;
		uint32 m_SubpassIndex = 0;

		CULL_MODE m_CullMode = CULL_MODE_BACK;
		bool m_FrontCCW = true;

		bool m_DepthEnable = true;
		bool m_DepthWriteEnable = true;
		COMPARISON_FUNCTION m_DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		MATERIAL_TEXTURE_BINDING_MODE m_TextureBindingMode = MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC;

		// Auto layout
		SHADER_RESOURCE_VARIABLE_TYPE m_DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		std::vector<ShaderResourceVariableDesc> m_Variables = {};
		std::vector<ImmutableSamplerDesc> m_ImmutableSamplers = {};

		// Fixed immutable sampler
		std::string m_LinearWrapSamplerName = "g_LinearWrapSampler";
		SamplerDesc m_LinearWrapSamplerDesc =
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
		};

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
