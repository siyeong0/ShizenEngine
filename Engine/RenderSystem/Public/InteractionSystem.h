#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
    class Renderer;
    class TerrainSystem;
    struct RenderPassContext;
    struct RenderPassBuilder;

    class InteractionSystem final
    {
    public:
        InteractionSystem() = default;
        ~InteractionSystem() = default;

        InteractionSystem(const InteractionSystem&) = delete;
        InteractionSystem& operator=(const InteractionSystem&) = delete;

        void InstallPasses(Renderer& renderer, TerrainSystem& terrain);

        float2 GetWorldOriginXZ() const;
        float2 GetWorldSizeXZ() const;
        uint2  GetTexelOrigin() const;

        uint32 GetInteractionFieldResolution() const { return INTERACTION_FIELD_RESOLUTION; }

    private:
        static constexpr uint32 INTERACTION_FIELD_RESOLUTION = 4096;
        static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 1024;

        static constexpr uint32 THREAD_GROUP_SIZE_X = 8;
        static constexpr uint32 THREAD_GROUP_SIZE_Y = 8;

        // World coverage of the interaction field (meters).
        static constexpr float  INTERACTION_FIELD_WORLD_SIZE = 256.0f;

        // ---------- Batch apply tuning ----------
        // Local-space binning tile size (texels). (tweak)
        static constexpr uint32 STAMP_BIN_TILE_SIZE = 32;

        // Max stamps per batch. Dispatch count ~= numStamps / BATCH_SIZE (roughly).
        static constexpr uint32 STAMP_BATCH_SIZE = 16;

        // Worst-case safety. (You can lower if you want stricter upper bound)
        static constexpr uint32 MAX_NUM_BATCHES = MAX_NUM_INTERACTION_STAMPS;

        // Shader file
        std::string m_InteractionCS = "InteractionCompute.hlsl";
        std::string m_DecayEntry = "DecayInteractionField";
        std::string m_ClearEntry = "ClearInteractionRect";
        std::string m_ApplyEntry = "ApplyInteractionBatchRect"; // changed

        // Persistent sliding state
        bool   m_bHasPrevOrigin = false;
        float2 m_PrevFieldOriginXZ = { 0.0f, 0.0f }; // snapped origin
        uint32 m_TexelOriginX = 0;
        uint32 m_TexelOriginY = 0;

        // PSOs
        RefCntAutoPtr<IPipelineState>         m_pDecayCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pDecaySRB;

        RefCntAutoPtr<IPipelineState>         m_pClearRectCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pClearRectSRB;

        RefCntAutoPtr<IPipelineState>         m_pApplyStampCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pApplyStampSRB;
    };
} // namespace shz
