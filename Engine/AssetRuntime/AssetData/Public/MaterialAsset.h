#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"

#include "Engine/AssetRuntime/Common/AssetRef.hpp"
#include "Engine/AssetRuntime/AssetData/Public/TextureAsset.h"

#include "Engine/RHI/Interface/ISampler.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Material/Public/MaterialTypes.h"

namespace shz
{
	class MaterialInstance;

	class MaterialAsset final
	{
	public:
		struct Options final : MaterialCommonOptions
		{
			bool bTwoSided = false;
			bool bCastShadow = true;
		};

		struct ValueOverride final
		{
			uint64 StableID = 0;
			std::string Name = {};
			MATERIAL_VALUE_TYPE Type = MATERIAL_VALUE_TYPE_UNKNOWN;
			std::vector<uint8> Data = {};
		};

		struct ResourceBinding final
		{
			uint64 StableID = 0;
			std::string Name = {};
			MATERIAL_RESOURCE_TYPE Type = MATERIAL_RESOURCE_TYPE_UNKNOWN;

			AssetRef<TextureAsset> TextureRef = {};

			// Optional sampler override (serialized)
			bool bHasSamplerOverride = false;
			SamplerDesc SamplerOverrideDesc =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};
		};

	public:
		MaterialAsset() = default;
		MaterialAsset(const MaterialAsset&) = default;
		MaterialAsset(MaterialAsset&&) noexcept = default;
		MaterialAsset& operator=(const MaterialAsset&) = default;
		MaterialAsset& operator=(MaterialAsset&&) noexcept = default;
		~MaterialAsset() = default;

		// Metadata
		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const noexcept { return m_Name; }

		void SetTemplateName(const std::string& name) { m_TemplateName = name; }
		const std::string& GetTemplateName() const noexcept { return m_TemplateName; }

		void SetRenderPassName(const std::string& name) { m_RenderPassName = name; }
		const std::string& GetRenderPassName() const noexcept { return m_RenderPassName; }

		// Options
		Options& GetOptions() noexcept { return m_Options; }
		const Options& GetOptions() const noexcept { return m_Options; }

		// Convenience setters
		void SetBlendMode(MATERIAL_BLEND_MODE mode) noexcept { m_Options.BlendMode = mode; }
		void SetCullMode(CULL_MODE mode) noexcept { m_Options.CullMode = mode; }
		void SetFrontCounterClockwise(bool v) noexcept { m_Options.FrontCounterClockwise = v; }

		void SetDepthEnable(bool v) noexcept { m_Options.DepthEnable = v; }
		void SetDepthWriteEnable(bool v) noexcept { m_Options.DepthWriteEnable = v; }
		void SetDepthFunc(COMPARISON_FUNCTION f) noexcept { m_Options.DepthFunc = f; }

		void SetTwoSided(bool v) noexcept { m_Options.bTwoSided = v; }
		void SetCastShadow(bool v) noexcept { m_Options.bCastShadow = v; }

		void SetTextureBindingMode(MATERIAL_TEXTURE_BINDING_MODE mode) noexcept { m_Options.TextureBindingMode = mode; }

		void SetLinearWrapSamplerName(const std::string& name) { m_Options.LinearWrapSamplerName = name.empty() ? "g_LinearWrapSampler" : name; }
		void SetLinearWrapSamplerDesc(const SamplerDesc& desc) { m_Options.LinearWrapSamplerDesc = desc; }

		// Values (stored as overrides)
		uint32 GetValueOverrideCount() const noexcept { return static_cast<uint32>(m_ValueOverrides.size()); }
		const ValueOverride& GetValueOverride(uint32 index) const { return m_ValueOverrides[index]; }

		const ValueOverride* FindValueOverride(const char* name) const;
		bool RemoveValueOverride(const char* name);

		bool SetFloat(const char* name, float v, uint64 stableId = 0);
		bool SetFloat2(const char* name, const float v[2], uint64 stableId = 0);
		bool SetFloat3(const char* name, const float v[3], uint64 stableId = 0);
		bool SetFloat4(const char* name, const float v[4], uint64 stableId = 0);

		bool SetInt(const char* name, int32 v, uint64 stableId = 0);
		bool SetInt2(const char* name, const int32 v[2], uint64 stableId = 0);
		bool SetInt3(const char* name, const int32 v[3], uint64 stableId = 0);
		bool SetInt4(const char* name, const int32 v[4], uint64 stableId = 0);

		bool SetUint(const char* name, uint32 v, uint64 stableId = 0);
		bool SetUint2(const char* name, const uint32 v[2], uint64 stableId = 0);
		bool SetUint3(const char* name, const uint32 v[3], uint64 stableId = 0);
		bool SetUint4(const char* name, const uint32 v[4], uint64 stableId = 0);

		bool SetFloat4x4(const char* name, const float m16[16], uint64 stableId = 0);

		bool SetRaw(const char* name, MATERIAL_VALUE_TYPE type, const void* pData, uint32 byteSize, uint64 stableId = 0);

		// Resources
		uint32 GetResourceBindingCount() const noexcept { return static_cast<uint32>(m_ResourceBindings.size()); }
		const ResourceBinding& GetResourceBinding(uint32 index) const { return m_ResourceBindings[index]; }

		const ResourceBinding* FindResourceBinding(const char* name) const;
		bool RemoveResourceBinding(const char* name);

		bool SetTextureAssetRef(
			const char* resourceName,
			MATERIAL_RESOURCE_TYPE expectedType,
			const AssetRef<TextureAsset>& textureRef,
			uint64 stableId = 0);

		bool SetSamplerOverride(
			const char* resourceName,
			const SamplerDesc& desc,
			uint64 stableId = 0);

		bool ClearSamplerOverride(const char* resourceName);

		// Reset / Validation
		void Clear();

		bool IsValid() const noexcept { return true; }

		// Apply to runtime instance
		bool ApplyToInstance(MaterialInstance* pInstance) const;

	private:
		ValueOverride* findValueOverrideMutable(const char* name);
		ResourceBinding* findResourceBindingMutable(const char* name);

		bool writeValueInternal(
			const char* name,
			MATERIAL_VALUE_TYPE type,
			const void* pData,
			uint32 byteSize,
			uint64 stableId);

	private:
		std::string m_Name = {};
		std::string m_TemplateName = {};
		std::string m_RenderPassName = "GBuffer";

		Options m_Options = {};

		std::vector<ValueOverride> m_ValueOverrides = {};
		std::vector<ResourceBinding> m_ResourceBindings = {};
	};

} // namespace shz
