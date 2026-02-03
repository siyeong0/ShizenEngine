#pragma once
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	class ShadowSystem final
	{
	public:
		ShadowSystem() = default;
		ShadowSystem(const ShadowSystem&) = delete;
		ShadowSystem& operator=(const ShadowSystem&) = delete;
		~ShadowSystem() = default;

		void InstallPasses(Renderer& renderer);

	private:
		// Shaders
		std::string m_ShadowVS = "Shadow.vsh";
		std::string m_ShadowPS = "Shadow.psh";
		std::string m_ShadowMaskedVS = "ShadowMasked.vsh";
		std::string m_ShadowMaskedPS = "ShadowMasked.psh";

		// PSO/SRB (Opaque)
		RefCntAutoPtr<IPipelineState> m_pShadowOpaquePSO;
		RefCntAutoPtr<IPipelineState> m_pShadowMaskedPSO;
	};
} // namespace shz
