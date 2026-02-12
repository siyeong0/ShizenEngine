#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/RenderSystem/Public/IndirectArgsSystem.h"

namespace shz
{
    class Renderer;
    class RenderScene;
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

        // Species API
        void AddGrassDesc(const GrassDesc& desc) { m_Species.push_back(desc); }
        void ClearGrassDescs() { m_Species.clear(); }

    private:
        static constexpr uint64 MAX_NUM_GRASS_LOD0_INSTANCES = 1u << 16;
        static constexpr uint64 MAX_NUM_GRASS_LOD1_INSTANCES = 1u << 18;
        static constexpr uint64 MAX_NUM_GRASS_LOD2_INSTANCES = 1u << 24;

        uint  m_ChunkHalfExtent = 32;
        float m_ChunkSize = 4.0f;
        uint  m_SamplesPerChunk = 128;

        // Per species indirect meshes (meshId is used for per-mesh count/offset)
        struct SpeciesIndirect final
        {
            IndirectArgsSystem::MeshHandle LOD0 = {};
            IndirectArgsSystem::MeshHandle LOD1 = {};
            IndirectArgsSystem::MeshHandle LOD2 = {};
        };

        std::vector<GrassDesc>       m_Species = {};
        std::vector<SpeciesIndirect> m_SpeciesIndirect = {};

        const InteractionSystem* m_pInteractionSystem = nullptr;

        RefCntAutoPtr<IPipelineState>         m_pUpdatePoolsCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pUpdatePoolsSRB;

        RefCntAutoPtr<IPipelineState>         m_pFillNewPoolsCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pFillNewPoolsSRB;

        RefCntAutoPtr<IPipelineState>         m_pCountInstancesCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pCountInstancesSRB;

        RefCntAutoPtr<IPipelineState>         m_pBuildInstancesCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pBuildInstancesSRB;

        // Spawn settings
        float m_YOffset = 0.00f;
        float m_Jitter = 0.95f;

        float m_MinPitch = -0.2f;
        float m_MaxPitch = 0.2f;
        float m_MinScale = 0.35f;
        float m_MaxScale = 0.55f;
        float m_SpawnProb = 1.0f;
        float m_SpawnRadius = 128.0f;

        float m_BendStrengthMin = 0.65f;
        float m_BendStrengthMax = 0.75f;
        uint  m_SeedSalt = 0xA53A9E37u;

        float m_DensityContrast = 0.28f;
        float m_DensityPow = 0.70f;
        float m_SlopeToDensity = 0.15f;

        float m_HeightMinN = 0.00f;
        float m_HeightMaxN = 1.00f;
        float m_HeightFadeN = 0.03f;

        std::string m_GrassGenCS = "GrassGenerateInstances.hlsl";
    };
} // namespace shz
