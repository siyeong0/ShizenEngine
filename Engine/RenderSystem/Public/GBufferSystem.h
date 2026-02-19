#pragma once
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

namespace shz
{
	class Renderer;

	// -----------------------------------------------------------------------------
	// GBufferSystem
	// - Writes GBuffers + depth
	// -----------------------------------------------------------------------------
	class GBufferSystem final
	{
	public:
		GBufferSystem() = default;
		GBufferSystem(const GBufferSystem&) = delete;
		GBufferSystem& operator=(const GBufferSystem&) = delete;
		~GBufferSystem() = default;

		void Initialize(Renderer& renderer);

		void InstallPasses(Renderer& renderer);
	};
} // namespace shz
