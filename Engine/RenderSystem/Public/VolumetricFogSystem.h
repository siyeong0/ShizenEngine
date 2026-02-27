#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	// -----------------------------------------------------------------------------
	// VolumetricFogSystem (Froxel-based)
	// - Pass0: Scatter  (Compute) -> FogVolume_Scatter (rgb=scatter, a=extinction)
	// - Pass1: Integrate(Compute) -> FogVolume_Integrated (rgb=L, a=T)
	// - Pass2: Temporal (Compute) -> FogVolume_Final (rgb=L, a=T) + History ping-pong
	// -----------------------------------------------------------------------------
	class VolumetricFogSystem final
	{
	public:
		VolumetricFogSystem() = default;
		VolumetricFogSystem(const VolumetricFogSystem&) = delete;
		VolumetricFogSystem& operator=(const VolumetricFogSystem&) = delete;
		~VolumetricFogSystem() = default;

		void Initialize(Renderer& renderer);
		void InstallPasses(Renderer& renderer);

	private:
		// Resources
		uint32 m_Downsample = 4;
		uint32 m_ZSlices    = 64;

		// Shaders
		std::string m_ScatterCS   = "VolumetricFogScatter.hlsl";
		std::string m_IntegrateCS = "VolumetricFogIntegrate.hlsl";
		std::string m_TemporalCS  = "VolumetricFogTemporal.hlsl";

		// PSO/SRB
		RefCntAutoPtr<IPipelineState> m_pScatterPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pScatterSRB;

		RefCntAutoPtr<IPipelineState> m_pIntegratePSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pIntegrateSRB;

		RefCntAutoPtr<IPipelineState> m_pTemporalPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pTemporalSRB;
	};
} // namespace shz