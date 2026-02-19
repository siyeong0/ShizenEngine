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
	// ScreenSpaceShadowSystem
	// - Reads: GBufferDepth, GBuffer1_Normal (optional but recommended)
	// - Writes: ScreenSpaceShadow (0..1)
	// -----------------------------------------------------------------------------
	class ContactShadowSystem final
	{
	public:
		ContactShadowSystem() = default;
		ContactShadowSystem(const ContactShadowSystem&) = delete;
		ContactShadowSystem& operator=(const ContactShadowSystem&) = delete;
		~ContactShadowSystem() = default;

		void Initialize(Renderer& renderer);

		void InstallPasses(Renderer& renderer);

	private:
		RefCntAutoPtr<IPipelineState> m_pSSSPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pSSSSRB;

		std::string m_FullscreenVS = "FullScreen.vsh";
		std::string m_SSSPS = "ScreenSpaceShadow.psh";
	};
} // namespace shz
