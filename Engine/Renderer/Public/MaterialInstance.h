#pragma once
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include "Engine/AssetRuntime/Public/AssetId.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

namespace shz
{
    enum MATERIAL_ALPHA_MODE : uint8
    {
        MATERIAL_ALPHA_OPAQUE = 0,
        MATERIAL_ALPHA_MASK,
        MATERIAL_ALPHA_BLEND
    };

    // ------------------------------------------------------------
    // MaterialInstance
    // - Runtime-side instance (CPU-side, no GPU dependency).
    // - References a parent material asset by AssetId (does NOT own/copy it).
    // - Stores optional overrides (texture asset refs + scalar/vector params).
    // ------------------------------------------------------------
    class MaterialInstance final
    {
    public:
        MaterialInstance() = default;

        explicit MaterialInstance(AssetId parent) noexcept
            : m_Parent(parent)
        {
        }

        // --------------------------------------------------------
        // Getters
        // --------------------------------------------------------
        float3 GetBaseColorFactor(const float3& fallback) const noexcept
        {
            return m_BaseColorFactor ? *m_BaseColorFactor : fallback;
        }

        float GetOpacity(float fallback) const noexcept
        {
            return m_Opacity ? *m_Opacity : fallback;
        }

        float GetMetallic(float fallback) const noexcept
        {
            return m_Metallic ? *m_Metallic : fallback;
        }

        float GetRoughness(float fallback) const noexcept
        {
            return m_Roughness ? *m_Roughness : fallback;
        }

        float GetNormalScale(float fallback) const noexcept
        {
            return m_NormalScale ? *m_NormalScale : fallback;
        }

        float GetOcclusionStrength(float fallback) const noexcept
        {
            return m_OcclusionStrength ? *m_OcclusionStrength : fallback;
        }

        float3 GetEmissiveFactor(const float3& fallback) const noexcept
        {
            return m_EmissiveFactor ? *m_EmissiveFactor : fallback;
        }

        MATERIAL_ALPHA_MODE GetAlphaMode(MATERIAL_ALPHA_MODE fallback) const noexcept
        {
            return m_AlphaMode ? *m_AlphaMode : fallback;
        }

        float GetAlphaCutoff(float fallback) const noexcept
        {
            return m_AlphaCutoff ? *m_AlphaCutoff : fallback;
        }

        // --------------------------------------------------------
        // Parent
        // --------------------------------------------------------
        void SetParent(AssetId parent) noexcept { m_Parent = parent; }
        AssetId GetParent() const noexcept { return m_Parent; }
        bool HasParent() const noexcept { return true; // TODO: }

        // --------------------------------------------------------
        // Texture overrides (asset references, AssetId)
        // --------------------------------------------------------
        void ClearAllTextureOverrides() noexcept
        {
            m_BaseColorTexture.reset();
            m_NormalTexture.reset();
            m_MetallicRoughnessTexture.reset();
            m_AmbientOcclusionTexture.reset();
            m_EmissiveTexture.reset();
        }

        void OverrideBaseColorTexture(AssetId tex) noexcept { m_BaseColorTexture = tex; }
        void OverrideNormalTexture(AssetId tex) noexcept { m_NormalTexture = tex; }
        void OverrideMetallicRoughnessTexture(AssetId tex) noexcept { m_MetallicRoughnessTexture = tex; }
        void OverrideAmbientOcclusionTexture(AssetId tex) noexcept { m_AmbientOcclusionTexture = tex; }
        void OverrideEmissiveTexture(AssetId tex) noexcept { m_EmissiveTexture = tex; }

        void ClearBaseColorTextureOverride() noexcept { m_BaseColorTexture.reset(); }
        void ClearNormalTextureOverride() noexcept { m_NormalTexture.reset(); }
        void ClearMetallicRoughnessTextureOverride() noexcept { m_MetallicRoughnessTexture.reset(); }
        void ClearAmbientOcclusionTextureOverride() noexcept { m_AmbientOcclusionTexture.reset(); }
        void ClearEmissiveTextureOverride() noexcept { m_EmissiveTexture.reset(); }

        bool HasBaseColorTextureOverride() const noexcept { return m_BaseColorTexture.has_value(); }
        bool HasNormalTextureOverride() const noexcept { return m_NormalTexture.has_value(); }
        bool HasMetallicRoughnessTextureOverride() const noexcept { return m_MetallicRoughnessTexture.has_value(); }
        bool HasAmbientOcclusionTextureOverride() const noexcept { return m_AmbientOcclusionTexture.has_value(); }
        bool HasEmissiveTextureOverride() const noexcept { return m_EmissiveTexture.has_value(); }

        AssetId GetBaseColorTextureOverrideOrInvalid() const noexcept { return m_BaseColorTexture.value_or(AssetId{}); }
        AssetId GetNormalTextureOverrideOrInvalid() const noexcept { return m_NormalTexture.value_or(AssetId{}); }
        AssetId GetMetallicRoughnessTextureOverrideOrInvalid() const noexcept { return m_MetallicRoughnessTexture.value_or(AssetId{}); }
        AssetId GetAmbientOcclusionTextureOverrideOrInvalid() const noexcept { return m_AmbientOcclusionTexture.value_or(AssetId{}); }
        AssetId GetEmissiveTextureOverrideOrInvalid() const noexcept { return m_EmissiveTexture.value_or(AssetId{}); }

        // --------------------------------------------------------
        // Parameter overrides
        // --------------------------------------------------------
        void ClearAllParameterOverrides() noexcept
        {
            m_BaseColorFactor.reset();
            m_Opacity.reset();
            m_Metallic.reset();
            m_Roughness.reset();
            m_NormalScale.reset();
            m_OcclusionStrength.reset();
            m_EmissiveFactor.reset();
            m_AlphaMode.reset();
            m_AlphaCutoff.reset();
        }

        void OverrideBaseColorFactor(const float3& v) noexcept { m_BaseColorFactor = v; }
        void OverrideOpacity(float v) noexcept { m_Opacity = v; }
        void OverrideMetallic(float v) noexcept { m_Metallic = v; }
        void OverrideRoughness(float v) noexcept { m_Roughness = v; }
        void OverrideNormalScale(float v) noexcept { m_NormalScale = v; }
        void OverrideOcclusionStrength(float v) noexcept { m_OcclusionStrength = v; }
        void OverrideEmissiveFactor(const float3& v) noexcept { m_EmissiveFactor = v; }

        void OverrideAlphaMode(MATERIAL_ALPHA_MODE mode) noexcept { m_AlphaMode = mode; }
        void OverrideAlphaCutoff(float v) noexcept { m_AlphaCutoff = v; }

        void ClearBaseColorFactorOverride() noexcept { m_BaseColorFactor.reset(); }
        void ClearOpacityOverride() noexcept { m_Opacity.reset(); }
        void ClearMetallicOverride() noexcept { m_Metallic.reset(); }
        void ClearRoughnessOverride() noexcept { m_Roughness.reset(); }
        void ClearNormalScaleOverride() noexcept { m_NormalScale.reset(); }
        void ClearOcclusionStrengthOverride() noexcept { m_OcclusionStrength.reset(); }
        void ClearEmissiveFactorOverride() noexcept { m_EmissiveFactor.reset(); }
        void ClearAlphaModeOverride() noexcept { m_AlphaMode.reset(); }
        void ClearAlphaCutoffOverride() noexcept { m_AlphaCutoff.reset(); }

        bool HasAlphaModeOverride() const noexcept { return m_AlphaMode.has_value(); }

        // Resolve 전에는 "최종"을 모름 → 단독 판단은 override가 있을 때만 의미 있음
        bool IsOpaqueOverrideOnly() const noexcept
        {
            return m_AlphaMode.has_value() ? (*m_AlphaMode == MATERIAL_ALPHA_OPAQUE) : true;
        }

        bool IsAlphaMaskedOverrideOnly() const noexcept
        {
            return m_AlphaMode.has_value() ? (*m_AlphaMode == MATERIAL_ALPHA_MASK) : false;
        }

        bool IsTranslucentOverrideOnly() const noexcept
        {
            return m_AlphaMode.has_value() ? (*m_AlphaMode == MATERIAL_ALPHA_BLEND) : false;
        }

    private:
        // Parent asset reference (MaterialAsset id)
        AssetId m_Parent = {};

        // Texture overrides (TextureAsset ids)
        std::optional<AssetId> m_BaseColorTexture;
        std::optional<AssetId> m_NormalTexture;
        std::optional<AssetId> m_MetallicRoughnessTexture;
        std::optional<AssetId> m_AmbientOcclusionTexture;
        std::optional<AssetId> m_EmissiveTexture;

        // Parameter overrides
        std::optional<float3> m_BaseColorFactor;
        std::optional<float>  m_Opacity;
        std::optional<float>  m_Metallic;
        std::optional<float>  m_Roughness;
        std::optional<float>  m_NormalScale;
        std::optional<float>  m_OcclusionStrength;
        std::optional<float3> m_EmissiveFactor;

        std::optional<MATERIAL_ALPHA_MODE> m_AlphaMode;
        std::optional<float>              m_AlphaCutoff;
    };
} // namespace shz
