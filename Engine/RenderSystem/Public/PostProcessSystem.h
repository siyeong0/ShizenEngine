#pragma once
#include <string>

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
		// TAA: 2 PSOs/SRBs (RenderPass attachment differs -> PSO must match pass renderpass)
		RefCntAutoPtr<IPipelineState>         m_pTAAPSO_H0;
		RefCntAutoPtr<IShaderResourceBinding> m_pTAASRB_H0;

		RefCntAutoPtr<IPipelineState>         m_pTAAPSO_H1;
		RefCntAutoPtr<IShaderResourceBinding> m_pTAASRB_H1;

		// Post: 1 PSO/SRB (reads History0/1, binds one at runtime)
		RefCntAutoPtr<IPipelineState>         m_pPostPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pPostSRB;

		std::string m_VS = "FullScreen.vsh";
		std::string m_TAAPS = "TemporalAA.psh";
		std::string m_PostPS = "PostCopy.psh";
	};
} // namespace shz
