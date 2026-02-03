#pragma once
#include <unordered_map>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Common/Public/HashUtils.hpp"

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IPipelineState.h"

#include "Engine/Renderer/Public/RenderResourceRegistry.h"

namespace shz
{
	class PipelineStateManager
	{
	public:
		PipelineStateManager() = default;
		PipelineStateManager(const PipelineStateManager&) = delete;
		PipelineStateManager& operator=(const PipelineStateManager&) = delete;
		~PipelineStateManager() { Clear(); }

		void Initialize(IRenderDevice* pDevice, RenderResourceRegistry* pResourceRegistry);
		void Clear();

		RefCntAutoPtr<IPipelineState> AcquireGraphics(const GraphicsPipelineStateCreateInfo& desc, bool bBindCommonResources = true);
		RefCntAutoPtr<IPipelineState> AcquireCompute(const ComputePipelineStateCreateInfo& desc, bool bBindCommonResources = true);

		void RegisterStaticTextureResource(const std::string& name, RenderResourceId id);

		void RegisterStaticBufferCBV(const std::string& name, RenderResourceId id); // ConstantBuffer
		void RegisterStaticBufferSRV(const std::string& name, RenderResourceId id); // Structured/Typed/ByteAddress SRV
		void RegisterStaticBufferUAV(const std::string& name, RenderResourceId id); // RWStructured/RWByteAddress UAV 


	private:
		void bindCommonStaticResources(IPipelineState* pPSO);

	private:
		IRenderDevice* m_pDevice = nullptr;
		RenderResourceRegistry* m_pResourceRegistry = nullptr;

		std::unordered_map<size_t, RefCntAutoPtr<IPipelineState>> m_GraphicsPSOMap;
		std::unordered_map<size_t, RefCntAutoPtr<IPipelineState>> m_ComputePSOMap;

		std::vector<std::pair<std::string, RenderResourceId>> m_CommonStaticTextureResources;
		std::unordered_map<std::string, RenderResourceId> m_CommonStaticBufferCBVs;
		std::unordered_map<std::string, RenderResourceId> m_CommonStaticBufferSRVs;
		std::unordered_map<std::string, RenderResourceId> m_CommonStaticBufferUAVs;
	};
} // namespace shz