#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

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
		RefCntAutoPtr<IPipelineState>         m_pSSSPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pSSSSRB;

		RefCntAutoPtr<IPipelineState>         m_pBlurCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pBlurSRB;

		std::string m_FullscreenVS = "FullScreen.vsh";
		std::string m_SSSPS = "ContactShadow.psh";

		std::string m_BlurCS = "BilinearBlur.hlsl";

	private:
		static constexpr uint32 BLUR_GROUP_SIZE_X = 8;
		static constexpr uint32 BLUR_GROUP_SIZE_Y = 8;
	};
} // namespace shz
