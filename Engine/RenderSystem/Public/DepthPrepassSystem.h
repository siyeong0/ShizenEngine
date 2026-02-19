#pragma once
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	class DepthPrepassSystem final
	{
	public:
		DepthPrepassSystem() = default;
		DepthPrepassSystem(const DepthPrepassSystem&) = delete;
		DepthPrepassSystem& operator=(const DepthPrepassSystem&) = delete;
		~DepthPrepassSystem() = default;

		void Initialize(Renderer& renderer);

		void InstallPasses(Renderer& renderer);
	};
} // namespace shz
