#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	class GrassSystem final
	{
	public:
		GrassSystem() = default;
		~GrassSystem() = default;

		GrassSystem(const GrassSystem&) = delete;
		GrassSystem& operator=(const GrassSystem&) = delete;

		void InstallPasses(Renderer& renderer);

		void SetGrassModle(const struct StaticMeshRenderData* mesh) { m_pGrassMesh = mesh; }

	private:
		static constexpr uint32 INTERACTION_FIELD_SIZE = 1025;
		static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 256;

		const struct StaticMeshRenderData* m_pGrassMesh = nullptr;

		// Interaction pass (2 CSOs)
		RefCntAutoPtr<IPipelineState>         m_pInteractionDecayCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pInteractionDecaySRB;

		RefCntAutoPtr<IPipelineState>         m_pInteractionApplyCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pInteractionApplySRB;

		// Generate instances
		RefCntAutoPtr<IPipelineState>         m_pGenCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGenSRB;

		// Write indirect args
		RefCntAutoPtr<IPipelineState>         m_pWriteArgsCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pWriteArgsSRB;

		// Render
		RefCntAutoPtr<IPipelineState>         m_pGrassPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassSRB;

		// Shaders
		std::string m_GrassGenCS = "GrassGenerateInstances.hlsl";
		std::string m_InteractionCS = "InteractionFieldUpdate.hlsl";
		std::string m_WriteArgsCS = "WriteIndirectArgs.hlsl";
		std::string m_GrassVS = "GrassForward.vsh";
		std::string m_GrassPS = "GrassForward.psh";
	};
} // namespace shz
