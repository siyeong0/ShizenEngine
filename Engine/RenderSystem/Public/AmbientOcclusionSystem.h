#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	class AmbientOcclusionSystem final
	{
	public:
		AmbientOcclusionSystem() = default;
		AmbientOcclusionSystem(const AmbientOcclusionSystem&) = delete;
		AmbientOcclusionSystem& operator=(const AmbientOcclusionSystem&) = delete;
		~AmbientOcclusionSystem() = default;

		void Initialize(Renderer& renderer);
		void InstallPasses(Renderer& renderer);

	private:
		// Pass0: GTAO Raw (Graphics)
		RefCntAutoPtr<IPipelineState>         m_pAOPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pAOSRB;

		// Pass1/2: Bilateral Blur (Compute, separable)
		RefCntAutoPtr<IPipelineState>         m_pBlurXCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pBlurXSRB;

		RefCntAutoPtr<IPipelineState>         m_pBlurYCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pBlurYSRB;

		std::string m_FullscreenVS = "FullScreen.vsh";
		std::string m_AOPS = "AmbientOcclusion.psh";

		// New bilateral blur compute shader
		std::string m_BilateralBlurCS = "AOBilateralBlur.hlsl";

	private:
		static constexpr uint32 BLUR_GROUP_SIZE_X = 8;
		static constexpr uint32 BLUR_GROUP_SIZE_Y = 8;
	};
} // namespace shz
