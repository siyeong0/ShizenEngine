#pragma once
#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"

#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/Renderer/Public/MaterialInstance.h"

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
    // - GPU-side binding data for a material instance (PSO/SRB + textures).
    // - Cached by Renderer.
    //
    // NOTE:
    // - This struct does NOT manage lifetime of the instance handle.
    // - Handle liveness must be validated by the cache/registry that owns it.
    // ------------------------------------------------------------
    struct MaterialRenderData final
    {
        // Identity (instance this render data belongs to)
        Handle<MaterialInstance> InstanceHandle = {};

        // Derived render policy
        MATERIAL_RENDER_QUEUE RenderQueue = MATERIAL_RENDER_QUEUE_OPAQUE;

        bool TwoSided = false;
        bool CastShadow = true;

        uint64 SortKey = 0;

        // GPU bindings
        RefCntAutoPtr<IPipelineState> pPSO;
        RefCntAutoPtr<IShaderResourceBinding> pSRB;

        RefCntAutoPtr<IBuffer> pMaterialCB;
        uint32 MaterialFlags = 0;

        RefCntAutoPtr<ISampler> pDefaultSampler;

        // Runtime constants
        float4 BaseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
        float  Metallic = 0.0f;
        float  Roughness = 0.5f;
        float  NormalScale = 1.0f;
        float  OcclusionStrength = 1.0f;
        float3 Emissive = float3(0.0f, 0.0f, 0.0f);
        float  AlphaCutoff = 0.5f;

        MaterialRenderData() = default;

        void Clear();
        bool IsValid() const noexcept;

        static MATERIAL_RENDER_QUEUE GetQueueFromAlphaMode(MATERIAL_ALPHA_MODE mode) noexcept;
    };
} // namespace shz
