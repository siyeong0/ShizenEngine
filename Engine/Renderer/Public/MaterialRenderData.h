#pragma once
#include "Primitives/BasicTypes.h"

#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/Renderer/Public/Material.h"

// RHI forward decls
namespace shz
{
    struct IPipelineState;
    struct IShaderResourceBinding;
    struct ITextureView;
    struct ISampler;

    enum MATERIAL_RENDER_QUEUE : uint8
    {
        MATERIAL_RENDER_QUEUE_OPAQUE = 0,
        MATERIAL_RENDER_QUEUE_MASKED,
        MATERIAL_RENDER_QUEUE_TRANSLUCENT,
    };

    // ------------------------------------------------------------
    // MaterialRenderData
    // - GPU-side binding data for a material (PSO/SRB + texture SRVs).
    // - Cached by Renderer (MaterialHandle -> MaterialRenderData).
    //
    // NOTE:
    // - Keep this strictly "render-implementation side".
    // - CPU authoring data lives in MaterialAsset.
    // - Runtime logical material params/handles live in Material.
    // ------------------------------------------------------------
    struct MaterialRenderData final
    {
        // ------------------------------------------------------------
        // Identity
        // ------------------------------------------------------------
        MaterialHandle Handle = {};

        // ------------------------------------------------------------
        // Derived render policy
        // ------------------------------------------------------------
        MATERIAL_RENDER_QUEUE RenderQueue = MATERIAL_RENDER_QUEUE_OPAQUE;

        // Optional: per-material raster state hints (if you plan to support it)
        bool TwoSided = false;
        bool CastShadow = true;

        // Sort key for render ordering / batching
        // Typical layout:
        //  [63..56] RenderQueue
        //  [55..40] PSOKey hash (or pipeline variant)
        //  [39.. 0] Material/Resource hash (textures/srb)
        uint64 SortKey = 0;

        // ------------------------------------------------------------
        // GPU bindings
        // ------------------------------------------------------------
        RefCntAutoPtr<IPipelineState>        pPSO;
        RefCntAutoPtr<IShaderResourceBinding> pSRB;

        // Common sampler (optional; could also be static sampler in PSO)
        RefCntAutoPtr<ISampler> pDefaultSampler;

        // ------------------------------------------------------------
        // Runtime constants (optional)
        // - If you later switch to "material uniform buffer",
        //   you can keep a pointer/offset here.
        // ------------------------------------------------------------
        float4 BaseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
        float  Metallic = 0.0f;
        float  Roughness = 0.5f;
        float  NormalScale = 1.0f;
        float  OcclusionStrength = 1.0f;
        float3 Emissive = float3(0.0f, 0.0f, 0.0f);
        float  AlphaCutoff = 0.5f;

        // ------------------------------------------------------------
        // Ctors
        // ------------------------------------------------------------
        MaterialRenderData() = default;

        // ------------------------------------------------------------
        // Helpers
        // ------------------------------------------------------------
        void Clear();
        bool IsValid() const noexcept;

        static MATERIAL_RENDER_QUEUE GetQueueFromAlphaMode(MATERIAL_ALPHA_MODE mode) noexcept;
    };
} // namespace shz
