#include "pch.h"
#include "Engine/Renderer/Public/PipelineStateManager.h"

namespace shz
{
	void PipelineStateManager::Initialize(IRenderDevice* pDevice, RenderResourceRegistry* pResourceRegistry)
	{
		m_pDevice = pDevice;
		m_pResourceRegistry = pResourceRegistry;
	}

	void PipelineStateManager::Clear()
	{
		m_GraphicsPSOMap.clear();
		m_ComputePSOMap.clear();
	}

	RefCntAutoPtr<IPipelineState> PipelineStateManager::AcquireGraphics(const GraphicsPipelineStateCreateInfo& desc, bool bBindCommonResources)
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

		if (bBindCommonResources)
		{
			bindCommonStaticResources(pso);
		}

		return m_GraphicsPSOMap[psoHash];
	}

	RefCntAutoPtr<IPipelineState> PipelineStateManager::AcquireCompute(const ComputePipelineStateCreateInfo& desc, bool bBindCommonResources)
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

		if (bBindCommonResources)
		{
			bindCommonStaticResources(pso);
		}

		return m_ComputePSOMap[psoHash];
	}

	void PipelineStateManager::RegisterStaticTextureResource(const std::string& name, RenderResourceId id)
	{
		ASSERT(m_pResourceRegistry->GetTexture(id) != nullptr, "Texture resource with ID %llu not found in registry", id);
		m_CommonStaticTextureResources.emplace_back(name, id);
	}
	void PipelineStateManager::RegisterStaticBufferResource(const std::string& name, RenderResourceId id)
	{
		ASSERT(m_pResourceRegistry->GetBuffer(id) != nullptr, "Buffer resource with ID %llu not found in registry", id);
		m_CommonStaticBufferResources.emplace_back(name, id);
	}

	void PipelineStateManager::bindCommonStaticResources(IPipelineState* pPSO)
	{
		SHADER_TYPE shaderTypes[] = {
			SHADER_TYPE_VERTEX,
			SHADER_TYPE_PIXEL,
			SHADER_TYPE_GEOMETRY,
			SHADER_TYPE_HULL,
			SHADER_TYPE_DOMAIN,
			SHADER_TYPE_COMPUTE,
			SHADER_TYPE_AMPLIFICATION,
			SHADER_TYPE_MESH,
			SHADER_TYPE_RAY_GEN,
			SHADER_TYPE_RAY_MISS,
			SHADER_TYPE_RAY_CLOSEST_HIT,
			SHADER_TYPE_RAY_ANY_HIT,
			SHADER_TYPE_RAY_INTERSECTION,
			SHADER_TYPE_CALLABLE,
			SHADER_TYPE_TILE
		};

		for (const auto& [name, id] : m_CommonStaticTextureResources)
		{
			ITextureView* pTextureView = m_pResourceRegistry->GetTextureSRV(id);
			ASSERT(pTextureView != nullptr, "Texture resource with ID %llu not found in registry", id);
			if (pTextureView)
			{
				for (SHADER_TYPE shaderType : shaderTypes)
				{
					IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(shaderType, name.c_str());
					if (pVar)
					{
						pVar->Set(pTextureView);
					}
				}
			}
		}
		for (const auto& [name, id] : m_CommonStaticBufferResources)
		{
			IBuffer* pBuffer = m_pResourceRegistry->GetBuffer(id);
			ASSERT(pBuffer != nullptr, "Buffer resource with ID %llu not found in registry", id);
			if (pBuffer)
			{
				for (SHADER_TYPE shaderType : shaderTypes)
				{
					IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(shaderType, name.c_str());
					if (pVar)
					{
						pVar->Set(pBuffer);
					}
				}
			}
		}

	}
} // namespace shz