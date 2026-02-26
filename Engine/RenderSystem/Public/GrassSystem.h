#pragma once
#include <string>
#include <vector>
#include <cstdint>

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
        // Selection weight (relative) within its group (base or special)
        float Weight = 1.0f;

        // Per-type scale / bend (shared across variations of this species)
        float MinScale = 0.30f;
        float MaxScale = 0.35f;

        float BendStrengthMin = 0.65f;
        float BendStrengthMax = 0.75f;

        // Clustering (macro patch) - used only for SPECIAL in this shader
        float ClusterStrength = 0.0f; // 0..1, 0 disables
        float ClusterScale = 16.0f;
        float ClusterJitter = 0.35f; // 0..1

        // Internal classification
        bool IsSpecial = false;

        std::vector<const StaticMeshRenderData*> Variations;

        uint32 GetNumVariations() const { return (uint32)Variations.size(); }
    };

    class GrassSystem final
    {
    public:
        GrassSystem() = default;
        ~GrassSystem() = default;

        GrassSystem(const GrassSystem&) = delete;
        GrassSystem& operator=(const GrassSystem&) = delete;

        void Initialize(Renderer& renderer);

        void InstallPasses(
            Renderer& renderer,
            RenderScene& scene,
            const InteractionSystem& interaction);

        void ClearGrassDescs();

        uint32 AddBaseGrass(const GrassDesc& desc);
        uint32 AddSpecialGrass(const GrassDesc& desc);

        uint32 GetNumSpecies() const { return (uint32)m_GrassDescs.size(); }
        uint32 GetNumTypes()   const { return (uint32)m_TypeToSpecies.size(); }

        uint32 GetNumBaseSpecies() const { return (uint32)m_BaseSpeciesIds.size(); }
        uint32 GetNumSpecialSpecies() const { return (uint32)m_SpecialSpeciesIds.size(); }

    private:
        static inline uint32 DivUp(uint32 x, uint32 d) { return (x + d - 1u) / d; }

        void RebuildGroupTables(); // base/special id lists

    private:
        static constexpr uint64 MAX_NUM_GRASS_LOD0_INSTANCES = 1u << 18;
        static constexpr uint64 MAX_NUM_GRASS_LOD1_INSTANCES = 1u << 20;
        static constexpr uint64 MAX_NUM_GRASS_LOD2_INSTANCES = 1u << 24;

        uint  m_ChunkHalfExtent = 128;
        float m_ChunkSize = 4.0f;

        uint  m_SamplesPerChunk = 1024;

        float m_LOD0Distance = 15.0f;
        float m_LOD1Distance = 45.0f;
        float m_LodHysteresis = 1.0f;

        struct TypeIndirect final
        {
            IndirectArgsSystem::MeshHandle LOD0 = {};
            IndirectArgsSystem::MeshHandle LOD1 = {};
            IndirectArgsSystem::MeshHandle LOD2 = {};
        };

        std::vector<TypeIndirect> m_TypeIndirect;

        const InteractionSystem* m_pInteractionSystem = nullptr;

        RefCntAutoPtr<IPipelineState>         m_pUpdatePoolsCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pUpdatePoolsSRB;

        RefCntAutoPtr<IPipelineState>         m_pFillNewPoolsCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pFillNewPoolsSRB;

        RefCntAutoPtr<IPipelineState>         m_pClearSpeciesCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pClearSpeciesSRB;

        RefCntAutoPtr<IPipelineState>         m_pCountSpeciesCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pCountSpeciesSRB;

        RefCntAutoPtr<IPipelineState>         m_pPrefixSpeciesCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pPrefixSpeciesSRB;

        RefCntAutoPtr<IPipelineState>         m_pBuildInstancesCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pBuildInstancesSRB;

        std::vector<GrassDesc> m_GrassDescs;

        // Species -> variations -> type flattening
        std::vector<uint32> m_SpeciesVarOffset; // size = numSpecies+1
        std::vector<uint32> m_SpeciesVarCount;  // size = numSpecies
        std::vector<uint32> m_TypeToSpecies;    // size = numTypes
        std::vector<uint32> m_TypeToVariation;  // size = numTypes

        // Base/special group lists (speciesId values)
        std::vector<uint32> m_BaseSpeciesIds;
        std::vector<uint32> m_SpecialSpeciesIds;

        float m_YOffset = 0.0f;
        float m_Jitter = 0.95f;
        float m_NormalAlignStrength = 0.75f;

        float m_SpawnRadius = 512.0f;
        uint  m_SeedSalt = 0xA53A9E37u;

        float m_DensityContrast = 0.28f;
        float m_DensityPow = 0.70f;

        std::string m_GrassGenCS = "GrassGenerateInstances.hlsl";
    };
} // namespace shz