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
	// LightingSystem
	// - Reads GBuffers (+ ShadowMapArray) and writes LightingScene
	// -----------------------------------------------------------------------------
	class LightingSystem final
	{
	public:
		LightingSystem() = default;
		LightingSystem(const LightingSystem&) = delete;
		LightingSystem& operator=(const LightingSystem&) = delete;
		~LightingSystem() = default;

		void Initialize(Renderer& renderer);

		void InstallPasses(Renderer& renderer);

	private:
		RefCntAutoPtr<IPipelineState> m_pLightingPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pLightingSRB;

		std::string m_LightingVS = "FullScreen.vsh";
		std::string m_LightingPS = "Lighting.psh";
	};
} // namespace shz
