#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;
	class IndirectArgsSystem;
	class TerrainSystem;
	struct StaticMeshRenderData;

	class GrassSystem final
	{
	public:
		GrassSystem() = default;
		~GrassSystem() = default;

		GrassSystem(const GrassSystem&) = delete;
		GrassSystem& operator=(const GrassSystem&) = delete;

		void InstallPasses(Renderer& renderer, IndirectArgsSystem& indirect, TerrainSystem& terrain);

		uint32 GetIndirectSlot() const { return m_IndirectSlot; }
		void SetGrassModel(const StaticMeshRenderData* pMesh) { m_pGrassMesh = pMesh; }

	private:
		static constexpr uint32 INTERACTION_FIELD_SIZE = 1025;
		static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 256;

		// Indirect slot for this system
		uint32 m_IndirectSlot = 0;

		// Interaction pass (2 CSOs)
		RefCntAutoPtr<IPipelineState>         m_pInteractionDecayCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pInteractionDecaySRB;

		RefCntAutoPtr<IPipelineState>         m_pInteractionApplyCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pInteractionApplySRB;

		// Generate instances
		RefCntAutoPtr<IPipelineState>         m_pGenCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGenSRB;

		// Render
		RefCntAutoPtr<IPipelineState>         m_pGrassPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassSRB;

		// Shadow
		RefCntAutoPtr<IPipelineState>         m_pGrassShadowPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassShadowSRB;

		// Mesh
		const StaticMeshRenderData* m_pGrassMesh = nullptr;

		// Shaders
		std::string m_GrassGenCS = "GrassGenerateInstances.hlsl";
		std::string m_InteractionCS = "InteractionFieldUpdate.hlsl";
		std::string m_GrassVS = "GrassForward.vsh";
		std::string m_GrassPS = "GrassForward.psh";
		std::string m_GrassShadowVS = "GrassShadow.vsh";		
		std::string m_ShadowPS = "ShadowMasked.psh";
	};
} // namespace shz
