#pragma once
#include <unordered_map>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Common/Public/HashUtils.hpp"

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IPipelineState.h"

namespace shz
{
	class PipelineStateManager
	{
	public:
		PipelineStateManager() = default;
		PipelineStateManager(const PipelineStateManager&) = delete;
		PipelineStateManager& operator=(const PipelineStateManager&) = delete;
		~PipelineStateManager() { Clear(); }

		void Initialize(IRenderDevice* pDevice);
		void Clear();

		RefCntAutoPtr<IPipelineState> AcquireGraphics(const GraphicsPipelineStateCreateInfo& desc);
		RefCntAutoPtr<IPipelineState> AcquireCompute(const ComputePipelineStateCreateInfo& desc);

	private:
		IRenderDevice* m_pDevice = nullptr;

		std::unordered_map<size_t, RefCntAutoPtr<IPipelineState>> m_GraphicsPSOMap;
		std::unordered_map<size_t, RefCntAutoPtr<IPipelineState>> m_ComputePSOMap;
	};
} // namespace shz