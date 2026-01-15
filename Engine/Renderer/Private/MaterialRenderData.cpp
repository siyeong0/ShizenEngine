#include "pch.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"

namespace shz
{
    MATERIAL_RENDER_QUEUE MaterialRenderData::GetQueueFromAlphaMode(MATERIAL_ALPHA_MODE mode) noexcept
    {
        switch (mode)
        {
        case MATERIAL_ALPHA_OPAQUE:
        {
            return MATERIAL_RENDER_QUEUE_OPAQUE;
        }
        case MATERIAL_ALPHA_MASK:
        {
            return MATERIAL_RENDER_QUEUE_MASKED;
        }
        case MATERIAL_ALPHA_BLEND:
        {
            return MATERIAL_RENDER_QUEUE_TRANSLUCENT;
        }
        default:
        {
            return MATERIAL_RENDER_QUEUE_OPAQUE;
        }
        }
    }

    void MaterialRenderData::Clear()
    {
        InstanceHandle = {};
        RenderQueue = MATERIAL_RENDER_QUEUE_OPAQUE;
        TwoSided = false;
        CastShadow = true;
        SortKey = 0;

        pPSO.Release();
        pSRB.Release();
        pDefaultSampler.Release();

        BaseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
        Metallic = 0.0f;
        Roughness = 0.5f;
        NormalScale = 1.0f;
        OcclusionStrength = 1.0f;
        Emissive = float3(0.0f, 0.0f, 0.0f);
        AlphaCutoff = 0.5f;
    }

    bool MaterialRenderData::IsValid() const noexcept
    {
        if (!InstanceHandle.IsValid())
        {
            return false;
        }

        if (!pPSO || !pSRB)
        {
            return false;
        }

        return true;
    }
} // namespace shz
