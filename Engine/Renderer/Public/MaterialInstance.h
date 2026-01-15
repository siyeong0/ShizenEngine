#pragma once
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Engine/Core/Math/Math.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

namespace shz
{
    // ------------------------------------------------------------
    // MaterialInstance
    // - Runtime-side instance (CPU-side, no GPU dependency).
    // - References a parent material asset by handle (does NOT own/copy it).
    // - Stores optional overrides (texture asset refs + scalar/vector params).
    //
    // NOTE:
    // - This class does NOT validate handle liveness.
    // - The resolver (AssetManager/Renderer) must check IsAlive() before dereferencing.
    // ------------------------------------------------------------
    class MaterialInstance final
    {
    public:
        MaterialInstance() = default;

        explicit MaterialInstance(Handle<MaterialAsset> parent) noexcept
            : m_Parent(parent)
        {
        }

        // --------------------------------------------------------
        // Parent
        // --------------------------------------------------------
        void SetParent(Handle<MaterialAsset> parent) noexcept
        {
            m_Parent = parent;
        }

        Handle<MaterialAsset> GetParent() const noexcept
        {
            return m_Parent;
        }

        // Returns true if the parent handle is non-zero (does not imply liveness).
        bool HasParentValue() const noexcept
        {
            return m_Parent.IsValid();
        }

        // --------------------------------------------------------
        // Texture overrides (asset references)
        // - "No override" is represented by an invalid handle.
        // --------------------------------------------------------
        void ClearAllTextureOverrides() noexcept
        {
            m_BaseColorTextureOverride = {};
            m_NormalTextureOverride = {};
            m_MetallicRoughnessTextureOverride = {};
            m_AmbientOcclusionTextureOverride = {};
            m_EmissiveTextureOverride = {};
        }

        void OverrideBaseColorTexture(Handle<TextureAsset> tex) noexcept
        {
            m_BaseColorTextureOverride = tex;
        }

        void OverrideNormalTexture(Handle<TextureAsset> tex) noexcept
        {
            m_NormalTextureOverride = tex;
        }

        void OverrideMetallicRoughnessTexture(Handle<TextureAsset> tex) noexcept
        {
            m_MetallicRoughnessTextureOverride = tex;
        }

        void OverrideAmbientOcclusionTexture(Handle<TextureAsset> tex) noexcept
        {
            m_AmbientOcclusionTextureOverride = tex;
        }

        void OverrideEmissiveTexture(Handle<TextureAsset> tex) noexcept
        {
            m_EmissiveTextureOverride = tex;
        }

        void ClearBaseColorTextureOverride() noexcept { m_BaseColorTextureOverride = {}; }
        void ClearNormalTextureOverride() noexcept { m_NormalTextureOverride = {}; }
        void ClearMetallicRoughnessTextureOverride() noexcept { m_MetallicRoughnessTextureOverride = {}; }
        void ClearAmbientOcclusionTextureOverride() noexcept { m_AmbientOcclusionTextureOverride = {}; }
        void ClearEmissiveTextureOverride() noexcept { m_EmissiveTextureOverride = {}; }

        bool HasBaseColorTextureOverride() const noexcept { return m_BaseColorTextureOverride.IsValid(); }
        bool HasNormalTextureOverride() const noexcept { return m_NormalTextureOverride.IsValid(); }
        bool HasMetallicRoughnessTextureOverride() const noexcept { return m_MetallicRoughnessTextureOverride.IsValid(); }
        bool HasAmbientOcclusionTextureOverride() const noexcept { return m_AmbientOcclusionTextureOverride.IsValid(); }
        bool HasEmissiveTextureOverride() const noexcept { return m_EmissiveTextureOverride.IsValid(); }

        Handle<TextureAsset> GetBaseColorTextureOverride() const noexcept { return m_BaseColorTextureOverride; }
        Handle<TextureAsset> GetNormalTextureOverride() const noexcept { return m_NormalTextureOverride; }
        Handle<TextureAsset> GetMetallicRoughnessTextureOverride() const noexcept { return m_MetallicRoughnessTextureOverride; }
        Handle<TextureAsset> GetAmbientOcclusionTextureOverride() const noexcept { return m_AmbientOcclusionTextureOverride; }
        Handle<TextureAsset> GetEmissiveTextureOverride() const noexcept { return m_EmissiveTextureOverride; }

        // --------------------------------------------------------
        // Parameter overrides
        // --------------------------------------------------------
        float3 GetBaseColorFactor(const float3& fallback) const noexcept
        {
            if (m_BaseColorFactor.has_value())
            {
                return *m_BaseColorFactor;
            }
            return fallback;
        }

        float GetOpacity(float fallback) const noexcept
        {
            if (m_Opacity.has_value())
            {
                return *m_Opacity;
            }
            return fallback;
        }

        float GetMetallic(float fallback) const noexcept
        {
            if (m_Metallic.has_value())
            {
                return *m_Metallic;
            }
            return fallback;
        }

        float GetRoughness(float fallback) const noexcept
        {
            if (m_Roughness.has_value())
            {
                return *m_Roughness;
            }
            return fallback;
        }

        float GetNormalScale(float fallback) const noexcept
        {
            if (m_NormalScale.has_value())
            {
                return *m_NormalScale;
            }
            return fallback;
        }

        float GetOcclusionStrength(float fallback) const noexcept
        {
            if (m_OcclusionStrength.has_value())
            {
                return *m_OcclusionStrength;
            }
            return fallback;
        }

        float3 GetEmissiveFactor(const float3& fallback) const noexcept
        {
            if (m_EmissiveFactor.has_value())
            {
                return *m_EmissiveFactor;
            }
            return fallback;
        }

        MATERIAL_ALPHA_MODE GetAlphaMode(MATERIAL_ALPHA_MODE fallback) const noexcept
        {
            if (m_AlphaMode.has_value())
            {
                return *m_AlphaMode;
            }
            return fallback;
        }

        float GetAlphaCutoff(float fallback) const noexcept
        {
            if (m_AlphaCutoff.has_value())
            {
                return *m_AlphaCutoff;
            }
            return fallback;
        }

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

        bool HasAlphaModeOverride() const noexcept
        {
            return m_AlphaMode.has_value();
        }

        // Returns true only when an override exists and equals the queried mode.
        bool IsOpaqueIfOverridden() const noexcept
        {
            if (!m_AlphaMode.has_value())
            {
                return false;
            }
            return (*m_AlphaMode == MATERIAL_ALPHA_OPAQUE);
        }

        bool IsMaskedIfOverridden() const noexcept
        {
            if (!m_AlphaMode.has_value())
            {
                return false;
            }
            return (*m_AlphaMode == MATERIAL_ALPHA_MASK);
        }

        bool IsBlendIfOverridden() const noexcept
        {
            if (!m_AlphaMode.has_value())
            {
                return false;
            }
            return (*m_AlphaMode == MATERIAL_ALPHA_BLEND);
        }

    private:
        Handle<MaterialAsset> m_Parent = {};

        Handle<TextureAsset> m_BaseColorTextureOverride = {};
        Handle<TextureAsset> m_NormalTextureOverride = {};
        Handle<TextureAsset> m_MetallicRoughnessTextureOverride = {};
        Handle<TextureAsset> m_AmbientOcclusionTextureOverride = {};
        Handle<TextureAsset> m_EmissiveTextureOverride = {};

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
