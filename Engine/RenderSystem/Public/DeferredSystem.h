#pragma once
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	class DeferredSystem final
	{
	public:
		DeferredSystem() = default;
		DeferredSystem(const DeferredSystem&) = delete;
		DeferredSystem& operator=(const DeferredSystem&) = delete;
		~DeferredSystem() = default;

		// Register all passes that belong to this system.
		void InstallPasses(Renderer& renderer);

	private:
		RefCntAutoPtr<IPipelineState> m_pLightingPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pLightingSRB;

		std::string m_LightingVS = "FullScreen.vsh";
		std::string m_LightingPS = "Lighting.psh";
	};
} // namespace shz
