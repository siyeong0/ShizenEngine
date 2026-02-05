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
	struct BillboardRenderData;

	struct GrassDesc final
	{
		float LOD0Distance = 12.0f;
		float LOD1Distance = 35.0f;
		float LodHysteresis = 1.0f;

		const StaticMeshRenderData* pMeshLod0 = nullptr;
		const StaticMeshRenderData* pCrossMeshLod1 = nullptr;
		const BillboardRenderData* pBillboardMeshLod2 = nullptr;
	};

	class GrassSystem final
	{
	public:
		GrassSystem() = default;
		~GrassSystem() = default;

		GrassSystem(const GrassSystem&) = delete;
		GrassSystem& operator=(const GrassSystem&) = delete;

		void InstallPasses(Renderer& renderer, IndirectArgsSystem& indirect, TerrainSystem& terrain);

		uint32 GetIndirectSlot() const { return m_IndirectSlot; }
		void SetGrassDesc(const GrassDesc& desc) { m_GrassDesc = desc; }

	private:
		static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 256;

		// Indirect slot for this system
		uint32 m_IndirectSlot = 0;

		// Generate instances
		RefCntAutoPtr<IPipelineState>         m_pGenCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGenSRB;

		// Render
		RefCntAutoPtr<IPipelineState>         m_pGrassPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassSRB;

		RefCntAutoPtr<IPipelineState>         m_pGrassBillboardPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassBillboardSRB;

		// Shadow
		RefCntAutoPtr<IPipelineState>         m_pGrassShadowPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassShadowSRB;

		// Copy Lighting(1x) -> GrassColorMSAA(4x)
		RefCntAutoPtr<IPipelineState>         m_pCopyToMSAAPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pCopyToMSAASRB;

		// Mesh
		GrassDesc m_GrassDesc = {};

		// Shaders
		std::string m_GrassGenCS = "GrassGenerateInstances.hlsl";
		std::string m_InteractionCS = "InteractionFieldUpdate.hlsl";
		std::string m_CopyVS = "Fullscreen.vsh";
		std::string m_CopyPS = "CopyTexture.psh";
		std::string m_GrassVS = "GrassForward.vsh";
		std::string m_GrassPS = "GrassForward.psh";
		std::string m_GrassShadowVS = "GrassShadow.vsh";		
		std::string m_ShadowPS = "ShadowMasked.psh";
		std::string m_GrassBillboardVS = "GrassBillboard.vsh";
		std::string m_GrassBillboardPS = "GrassBillboard.psh";
	};
} // namespace shz
