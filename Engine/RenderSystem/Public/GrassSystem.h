// ============================================================================
// GrassSystem.h
// - Removed per-species LOD distances (now global in GrassSystem)
// - Added per-species spawn params: Min/MaxScale, BendStrengthMin/Max
// - Removed unused global spawn params (SpawnProb etc.)
// ============================================================================
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

	// ------------------------------------------------------------
	// Grass description
	// - Weight: species selection probability (relative weight)
	// - Variations: chosen uniformly 1/N once species is chosen
	// - Per-species spawn params live here (scale/bend)
	// ------------------------------------------------------------
	struct GrassDesc final
	{
		float Weight = 1.0f;

		// Per-species params (applied to all variations of this species)
		float MinScale = 0.30f;
		float MaxScale = 0.35f;

		float BendStrengthMin = 0.65f;
		float BendStrengthMax = 0.75f;

		// Variation meshes (each variation has full LOD chain)
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

		// -----------------------------------------------------------------
		// Species API
		// -----------------------------------------------------------------
		void ClearGrassDescs();
		uint32 AddGrassDesc(const GrassDesc& desc);

		uint32 GetNumSpecies() const { return (uint32)m_GrassDescs.size(); }
		uint32 GetNumTypes()   const { return (uint32)m_TypeToSpecies.size(); } // (species,variation) flattened types

	private:
		static inline uint32 DivUp(uint32 x, uint32 d) { return (x + d - 1u) / d; }

	private:
		// ------------------------------------------------------------
		// Limits (render instance buffers)
		// ------------------------------------------------------------
		static constexpr uint64 MAX_NUM_GRASS_LOD0_INSTANCES = 1u << 18;
		static constexpr uint64 MAX_NUM_GRASS_LOD1_INSTANCES = 1u << 20;
		static constexpr uint64 MAX_NUM_GRASS_LOD2_INSTANCES = 1u << 24;

		// ------------------------------------------------------------
		// Chunk pool config (VisibleDim = 2*HalfExtent)
		// ------------------------------------------------------------
		uint  m_ChunkHalfExtent = 32;
		float m_ChunkSize = 4.0f;

		uint  m_SamplesPerChunk = 2048;

		// ------------------------------------------------------------
		// GLOBAL LOD distances (moved out of GrassDesc)
		// ------------------------------------------------------------
		float m_LOD0Distance = 12.0f;
		float m_LOD1Distance = 35.0f;
		float m_LodHysteresis = 1.0f;

		// ------------------------------------------------------------
		// Per-type indirect mesh handles (per LOD)
		// "type" = (species, variation) flattened
		// ------------------------------------------------------------
		struct TypeIndirect final
		{
			IndirectArgsSystem::MeshHandle LOD0 = {};
			IndirectArgsSystem::MeshHandle LOD1 = {};
			IndirectArgsSystem::MeshHandle LOD2 = {};
		};

		std::vector<TypeIndirect> m_TypeIndirect;

		// ------------------------------------------------------------
		// Interaction system reference
		// ------------------------------------------------------------
		const InteractionSystem* m_pInteractionSystem = nullptr;

		// ------------------------------------------------------------
		// ChunkPool PSOs/SRBs (Compute only)
		// ------------------------------------------------------------
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

		// ------------------------------------------------------------
		// Species descs
		// ------------------------------------------------------------
		std::vector<GrassDesc> m_GrassDescs;

		// ------------------------------------------------------------
		// Flattened type mapping (CPU)
		// typeId -> (speciesId, variationId)
		// Also keep per-species variation offset/count for GPU tables.
		// ------------------------------------------------------------
		std::vector<uint32> m_SpeciesVarOffset; // size = numSpecies+1
		std::vector<uint32> m_SpeciesVarCount;  // size = numSpecies
		std::vector<uint32> m_TypeToSpecies;    // size = numTypes
		std::vector<uint32> m_TypeToVariation;  // size = numTypes

		// ------------------------------------------------------------
		// Spawn settings (GLOBAL)
		// ------------------------------------------------------------
		float m_YOffset = 0.0f;
		float m_Jitter = 0.95f;
		float m_NormalAlignStrength = 0.75f;

		float m_SpawnRadius = 128.0f;
		uint  m_SeedSalt = 0xA53A9E37u;

		// density shaping
		float m_DensityContrast = 0.28f;
		float m_DensityPow = 0.70f;

		// height masks
		float m_HeightMinN = 0.00f;
		float m_HeightMaxN = 1.00f;
		float m_HeightFadeN = 0.03f;

		// ------------------------------------------------------------
		// Shader path (single file, multiple entry points)
		// ------------------------------------------------------------
		std::string m_GrassGenCS = "GrassGenerateInstances.hlsl";
	};
} // namespace shz