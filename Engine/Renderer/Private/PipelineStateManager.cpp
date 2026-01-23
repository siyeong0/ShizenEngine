#include "pch.h"
#include "Engine/Renderer/Public/PipelineStateManager.h"

namespace shz
{
	void PipelineStateManager::Initialize(IRenderDevice* pDevice)
	{
		m_pDevice = pDevice;
	}

	void PipelineStateManager::Clear()
	{
		m_GraphicsPSOMap.clear();
		m_ComputePSOMap.clear();
	}

	RefCntAutoPtr<IPipelineState> PipelineStateManager::AcquireGraphics(const GraphicsPipelineStateCreateInfo& desc)
	{
		std::hash<GraphicsPipelineStateCreateInfo> hasher;
		size_t psoHash = hasher(desc);

		auto it = m_GraphicsPSOMap.find(psoHash);
		if (it != m_GraphicsPSOMap.end())
		{
			return it->second;
		}

		RefCntAutoPtr<IPipelineState> pso;
		m_pDevice->CreateGraphicsPipelineState(desc, &pso);
		ASSERT(pso, "Failed to create graphics pipeline state");
		m_GraphicsPSOMap[psoHash] = pso;

		return m_GraphicsPSOMap[psoHash];
	}

	RefCntAutoPtr<IPipelineState> PipelineStateManager::AcquireCompute(const ComputePipelineStateCreateInfo& desc)
	{
		std::hash<ComputePipelineStateCreateInfo> hasher;
		size_t psoHash = hasher(desc);

		auto it = m_ComputePSOMap.find(psoHash);
		if (it != m_ComputePSOMap.end())
		{
			return it->second;
		}

		RefCntAutoPtr<IPipelineState> pso;
		m_pDevice->CreateComputePipelineState(desc, &pso);
		ASSERT(pso, "Failed to create compute pipeline state");
		m_ComputePSOMap[psoHash] = pso;

		return m_ComputePSOMap[psoHash];
	}
} // namespace shz