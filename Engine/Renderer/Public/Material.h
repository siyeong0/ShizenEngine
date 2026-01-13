#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

namespace shz
{
    struct TextureHandle
    {
        uint32 Id = 0; // 0 = invalid
        constexpr bool IsValid() const { return Id != 0; }
    };

    enum MATERIAL_PRESET_TYPE
    {
        MATERIAL_TYPE_DEFAULT,
        MATERIAL_TYPE_MATTE,
        MATERIAL_TYPE_MIRROR,
        MATERIAL_TYPE_PLASTIC,
        MATERIAL_TYPE_GLASS,
        MATERIAL_TYPE_WATER,
        MATERIAL_TYPE_METAL,
        MATERIAL_TYPE_COUNT
    };

    enum MATERIAL_ALPHA_MODE
    {
        MATERIAL_ALPHA_OPAQUE, // 완전 불투명
        MATERIAL_ALPHA_MASK,   // 알파 테스트(컷아웃)
        MATERIAL_ALPHA_BLEND   // 알파 블렌딩(투명)
    };

    struct Material
    {
        // =========================================================
        // Textures (참조만: 소유 X)
        // =========================================================
        // sRGB
        TextureHandle BaseColorTexture = {};
        // Linear
        TextureHandle NormalTexture = {};
        // Linear: (Metallic, Roughness) packed (예: R=Metallic, G=Roughness)
        TextureHandle MetallicRoughnessTexture = {};
        // Linear
        TextureHandle AmbientOcclusionTexture = {};
        // sRGB or Linear(엔진 정책에 맞게)
        TextureHandle EmissiveTexture = {};
        // Optional: 알파를 별도 텍스처로 관리하고 싶을 때
        TextureHandle OpacityTexture = {};

        // =========================================================
        // Factors (Metallic-Roughness PBR)
        // =========================================================
        float3 BaseColorFactor = DEFAULT_BASE_COLOR;
        float  Opacity = DEFAULT_OPACITY;

        float  MetallicFactor = DEFAULT_METALLIC;
        float  RoughnessFactor = DEFAULT_ROUGHNESS;

        float  NormalScale = DEFAULT_NORMAL_SCALE;
        float  AmbientOcclusionStrength = DEFAULT_AO_STRENGTH;

        float3 EmissiveFactor = DEFAULT_EMISSIVE;

        // =========================================================
        // Alpha
        // =========================================================

        MATERIAL_ALPHA_MODE AlphaMode = MATERIAL_ALPHA_OPAQUE;
        float AlphaCutoff = DEFAULT_ALPHA_CUTOFF; // AlphaMode==MASK일 때 사용

        // =========================================================
        // Ctors
        // =========================================================

        Material() = default;

        Material(
            const float3& baseColorFactor,
            float         opacity,
            float         metallic,
            float         roughness,
            MATERIAL_ALPHA_MODE alphaMode = MATERIAL_ALPHA_OPAQUE,
            float         alphaCutoff = DEFAULT_ALPHA_CUTOFF)
            : BaseColorFactor(baseColorFactor)
            , Opacity(opacity)
            , MetallicFactor(metallic)
            , RoughnessFactor(roughness)
            , AlphaMode(alphaMode)
            , AlphaCutoff(alphaCutoff)
        {
        }

        Material(
            MATERIAL_PRESET_TYPE type,
            MATERIAL_ALPHA_MODE  alphaMode = MATERIAL_ALPHA_OPAQUE)
            : AlphaMode(alphaMode)
        {
            ResetToDefaults();
            ApplyPreset(type);
        }

        // =========================================================
        // Helpers
        // =========================================================

        void ResetToDefaults()
        {
            BaseColorTexture = {};
            NormalTexture = {};
            MetallicRoughnessTexture = {};
            AmbientOcclusionTexture = {};
            EmissiveTexture = {};
            OpacityTexture = {};

            BaseColorFactor = DEFAULT_BASE_COLOR;
            Opacity = DEFAULT_OPACITY;

            MetallicFactor = DEFAULT_METALLIC;
            RoughnessFactor = DEFAULT_ROUGHNESS;

            NormalScale = DEFAULT_NORMAL_SCALE;
            AmbientOcclusionStrength = DEFAULT_AO_STRENGTH;

            EmissiveFactor = DEFAULT_EMISSIVE;

            AlphaMode = MATERIAL_ALPHA_OPAQUE;
            AlphaCutoff = DEFAULT_ALPHA_CUTOFF;
        }

        void ApplyPreset(MATERIAL_PRESET_TYPE type)
        {
            switch (type)
            {
            case MATERIAL_TYPE_DEFAULT:
                // defaults 유지
                break;

            case MATERIAL_TYPE_MATTE:
                BaseColorFactor = { 0.75f, 0.75f, 0.75f };
                MetallicFactor = 0.0f;
                RoughnessFactor = 0.90f;
                break;

            case MATERIAL_TYPE_MIRROR:
                // “이상적인 거울”은 사실 금속/유전체로 딱 떨어지진 않지만,
                // 엔진 프리셋으로는 매우 낮은 roughness + 높은 metallic로 근사
                BaseColorFactor = { 1.0f, 1.0f, 1.0f };
                MetallicFactor = 1.0f;
                RoughnessFactor = 0.02f;
                break;

            case MATERIAL_TYPE_PLASTIC:
                // 유전체(비금속)
                BaseColorFactor = { 0.8f, 0.1f, 0.1f };
                MetallicFactor = 0.0f;
                RoughnessFactor = 0.35f;
                break;

            case MATERIAL_TYPE_GLASS:
                // 투명은 보통 BLEND(혹은 별도 투명 파이프라인)
                BaseColorFactor = { 1.0f, 1.0f, 1.0f };
                MetallicFactor = 0.0f;
                RoughnessFactor = 0.02f;
                Opacity = 0.05f;
                AlphaMode = MATERIAL_ALPHA_BLEND;
                AmbientOcclusionStrength = 0.10f;
                break;

            case MATERIAL_TYPE_WATER:
                BaseColorFactor = { 0.8f, 0.8f, 1.0f };
                MetallicFactor = 0.0f;
                RoughnessFactor = 0.02f;
                NormalScale = 1.50f;
                Opacity = 0.10f;
                AlphaMode = MATERIAL_ALPHA_BLEND;
                AmbientOcclusionStrength = 0.05f;
                break;

            case MATERIAL_TYPE_METAL:
                BaseColorFactor = { 0.9f, 0.9f, 0.9f };
                MetallicFactor = 1.0f;
                RoughnessFactor = 0.15f;
                break;

            default:
                ASSERT(false, "Unknown material preset type.");
                break;
            }
        }

        bool IsOpaque() const
        {
            // OPAQUE면 무조건 불투명 취급
            if (AlphaMode == MATERIAL_ALPHA_OPAQUE)
                return true;

            // MASK는 컷아웃이므로 "불투명 패스"에 넣고 싶을 때가 많음(엔진 정책)
            // 여기서는 “완전 불투명”만 true로 정의
            return false;
        }

        bool IsAlphaMasked() const { return AlphaMode == MATERIAL_ALPHA_MASK; }
        bool IsTranslucent() const { return AlphaMode == MATERIAL_ALPHA_BLEND; }

    private:
        // Physically plausible defaults (dielectric, mid-roughness)
        static inline constexpr float3 DEFAULT_BASE_COLOR = { 0.8f, 0.8f, 0.8f };
        static inline constexpr float  DEFAULT_OPACITY = 1.0f;

        static inline constexpr float  DEFAULT_METALLIC = 0.0f;
        static inline constexpr float  DEFAULT_ROUGHNESS = 0.5f;

        static inline constexpr float  DEFAULT_NORMAL_SCALE = 1.0f;
        static inline constexpr float  DEFAULT_AO_STRENGTH = 1.0f;

        static inline constexpr float3 DEFAULT_EMISSIVE = { 0.0f, 0.0f, 0.0f };

        static inline constexpr float  DEFAULT_ALPHA_CUTOFF = 0.5f;
    };
} // namespace shz
