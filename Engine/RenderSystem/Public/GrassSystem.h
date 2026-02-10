#pragma once
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
    class Renderer;
    class RenderScene;
    class IndirectArgsSystem;
    class InteractionSystem;
    struct StaticMeshRenderData;

    struct GrassDesc final
    {
        float LOD0Distance = 12.0f;
        float LOD1Distance = 35.0f;
        float LodHysteresis = 1.0f;

        const StaticMeshRenderData* pMeshLod0 = nullptr;
        const StaticMeshRenderData* pCrossMeshLod1 = nullptr;
        const StaticMeshRenderData* pBillboardMeshLod2 = nullptr;
    };

    class GrassSystem final
    {
    public:
        GrassSystem() = default;
        ~GrassSystem() = default;

        GrassSystem(const GrassSystem&) = delete;
        GrassSystem& operator=(const GrassSystem&) = delete;

        void InstallPasses(
            Renderer& renderer,
            RenderScene& scene,
            IndirectArgsSystem& indirect,
            const InteractionSystem& interaction);

        void SetGrassDesc(const GrassDesc& desc) { m_GrassDesc = desc; }

    private:
        static constexpr uint64 MAX_NUM_GRASS_LOD0_INSTANCES = 1u << 16;
        static constexpr uint64 MAX_NUM_GRASS_LOD1_INSTANCES = 1u << 18;
        static constexpr uint64 MAX_NUM_GRASS_LOD2_INSTANCES = 1u << 24;

        // Indirect slot for this system
        uint32 m_IndirectSlotLOD0 = 0;
        uint32 m_IndirectSlotLOD1 = 0;
        uint32 m_IndirectSlotLOD2 = 0;

        // Interaction system reference
        const InteractionSystem* m_pInteractionSystem = nullptr;

        // Generate instances (Compute only)
        RefCntAutoPtr<IPipelineState>         m_pGenCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pGenSRB;

        // Mesh desc
        GrassDesc m_GrassDesc = {};

        // Settings
        float m_YOffset = -0.05f;

        float m_ChunkSize = 4.0f;
        uint  m_ChunkHalfExtent = 64;
        uint  m_SamplesPerChunk = 1024;
        float m_Jitter = 0.95f;

        float m_MinPitch = -0.2f;
        float m_MaxPitch = 0.2f;
        float m_MinScale = 7.7f;
        float m_MaxScale = 13.1f;
        float m_SpawnProb = 1.0f;
        float m_SpawnRadius = 256.0f;

        float m_BendStrengthMin = 0.65f;
        float m_BendStrengthMax = 0.75f;
        uint  m_SeedSalt = 0xA53A9E37u;

        float m_DensityTiling = 0.02f;
        float m_DensityContrast = 0.28f;
        float m_DensityPow = 0.70f;
        float m_SlopeToDensity = 0.15f;

        float m_HeightMinN = 0.00f;
        float m_HeightMaxN = 1.00f;
        float m_HeightFadeN = 0.03f;

        // Shaders
        std::string m_GrassGenCS = "GrassGenerateInstances.hlsl";
    };
} // namespace shz
