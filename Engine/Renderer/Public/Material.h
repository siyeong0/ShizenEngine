#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Renderer/Public/Handles.h"

namespace shz
{
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
        // Textures
        // =========================================================
        TextureHandle BaseColorTexture = {};
        TextureHandle NormalTexture = {};
        TextureHandle MetallicRoughnessTexture = {};
        TextureHandle AmbientOcclusionTexture = {};
        TextureHandle EmissiveTexture = {};

        // =========================================================
        // Factors (Metallic-Roughness PBR)
        // =========================================================
        float3 BaseColorFactor = { 0.8f, 0.8f, 0.8f };
        float  Opacity = 1.0f;
        float  MetallicFactor = 0.0f;
        float  RoughnessFactor = 0.5f;
        float  NormalScale = 1.0f;
        float  AmbientOcclusionStrength = 1.0f;
        float3 EmissiveFactor = { 0.0f, 0.0f, 0.0f };

        // =========================================================
        // Alpha
        // =========================================================

        MATERIAL_ALPHA_MODE AlphaMode = MATERIAL_ALPHA_OPAQUE;
        float AlphaCutoff = 0.5f; // AlphaMode==MASK일 때 사용

        // =========================================================
        // Ctors
        // =========================================================

        Material() = default;

        Material(
            const float3& baseColorFactor,
            float opacity,
            float metallic,
            float roughness,
            MATERIAL_ALPHA_MODE alphaMode = MATERIAL_ALPHA_OPAQUE,
            float alphaCutoff = 0.5f)
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
            *this = {};
            ApplyPreset(type);
        }

        // =========================================================
        // Helpers
        // =========================================================

        void ApplyPreset(MATERIAL_PRESET_TYPE type)
        {
            switch (type)
            {
            case MATERIAL_TYPE_DEFAULT:
                break;

            case MATERIAL_TYPE_MATTE:
                BaseColorFactor = { 0.75f, 0.75f, 0.75f };
                MetallicFactor = 0.0f;
                RoughnessFactor = 0.90f;
                break;

            case MATERIAL_TYPE_MIRROR:
                BaseColorFactor = { 1.0f, 1.0f, 1.0f };
                MetallicFactor = 1.0f;
                RoughnessFactor = 0.02f;
                break;

            case MATERIAL_TYPE_PLASTIC:
                BaseColorFactor = { 0.8f, 0.1f, 0.1f };
                MetallicFactor = 0.0f;
                RoughnessFactor = 0.35f;
                break;

            case MATERIAL_TYPE_GLASS:
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
            if (AlphaMode == MATERIAL_ALPHA_OPAQUE)
                return true;
            return false;
        }

        bool IsAlphaMasked() const { return AlphaMode == MATERIAL_ALPHA_MASK; }
        bool IsTranslucent() const { return AlphaMode == MATERIAL_ALPHA_BLEND; }
    };
} // namespace shz
