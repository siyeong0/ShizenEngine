#pragma once
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	class PostProcessSystem final
	{
	public:
		PostProcessSystem() = default;
		PostProcessSystem(const PostProcessSystem&) = delete;
		PostProcessSystem& operator=(const PostProcessSystem&) = delete;
		~PostProcessSystem() = default;

		void InstallPasses(Renderer& renderer);

	private:
		RefCntAutoPtr<IPipelineState>         m_pPostPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pPostSRB;

		std::string m_VS = "FullScreen.vsh";
		std::string m_PS = "PostCopy.psh";
	};
} // namespace shz
