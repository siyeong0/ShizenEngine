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
		ASSERT(!name.empty(), "name is empty.");
		ASSERT(m_pResourceRegistry->GetTexture(id) != nullptr, "Texture resource with ID %llu not found in registry", id);
		m_CommonStaticTextureResources.emplace_back(name, id);
	}

	void PipelineStateManager::RegisterStaticBufferCBV(const std::string& name, RenderResourceId id)
	{
		ASSERT(!name.empty(), "name is empty.");
		ASSERT(m_pResourceRegistry->GetBuffer(id) != nullptr, "Buffer resource with ID %llu not found in registry", id);
		m_CommonStaticBufferCBVs[name] = id;
	}

	void PipelineStateManager::RegisterStaticBufferSRV(const std::string& name, RenderResourceId id)
	{
		ASSERT(!name.empty(), "name is empty.");
		ASSERT(m_pResourceRegistry->GetBuffer(id) != nullptr, "Buffer resource with ID %llu not found in registry", id);
		m_CommonStaticBufferSRVs[name] = id;
	}

	void PipelineStateManager::RegisterStaticBufferUAV(const std::string& name, RenderResourceId id)
	{
		ASSERT(!name.empty(), "name is empty.");
		ASSERT(m_pResourceRegistry->GetBuffer(id) != nullptr, "Buffer resource with ID %llu not found in registry", id);
		m_CommonStaticBufferUAVs[name] = id;
	}

	void PipelineStateManager::bindCommonStaticResources(IPipelineState* pPSO)
	{
		ASSERT(pPSO, "PSO is null.");
		ASSERT(m_pResourceRegistry, "ResourceRegistry is null.");

		const SHADER_TYPE SHADER_TYPES[] =
		{
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

		auto bindStaticVarAllStages = [&](const std::string& name, auto* pObject)
		{
			for (SHADER_TYPE st : SHADER_TYPES)
			{
				if (IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(st, name.c_str()))
				{
					pVar->Set(pObject);
				}
			}
		};

		// ------------------------------------------------------------
		// Textures (SRV)
		// ------------------------------------------------------------
		for (const auto& [name, id] : m_CommonStaticTextureResources)
		{
			ITextureView* pSRV = m_pResourceRegistry->GetTextureSRV(id);
			ASSERT(pSRV != nullptr, "Texture SRV '%s' (id=%llu) not found.", name.c_str(), id);
			bindStaticVarAllStages(name, pSRV);
		}

		// ------------------------------------------------------------
		// Buffers: CBV (Constant / Uniform buffer) -> IBuffer*
		// ------------------------------------------------------------
		for (const auto& [name, id] : m_CommonStaticBufferCBVs)
		{
			IBuffer* pBuf = m_pResourceRegistry->GetBuffer(id);
			ASSERT(pBuf != nullptr, "Buffer(CBV) '%s' (id=%llu) not found.", name.c_str(), id);
			bindStaticVarAllStages(name, pBuf);
		}

		// ------------------------------------------------------------
		// Buffers: SRV (StructuredBuffer / ByteAddressBuffer / Typed Buffer SRV) -> IBufferView*
		// ------------------------------------------------------------
		for (const auto& [name, id] : m_CommonStaticBufferSRVs)
		{
			IBufferView* pSRV = m_pResourceRegistry->GetBufferSRV(id);
			ASSERT(pSRV != nullptr, "Buffer SRV '%s' (id=%llu) not found.", name.c_str(), id);
			bindStaticVarAllStages(name, pSRV);
		}

		// ------------------------------------------------------------
		// Buffers: UAV (RWStructuredBuffer / RWByteAddressBuffer / Typed UAV) -> IBufferView*
		// ------------------------------------------------------------
		for (const auto& [name, id] : m_CommonStaticBufferUAVs)
		{
			IBufferView* pUAV = m_pResourceRegistry->GetBufferUAV(id);
			ASSERT(pUAV != nullptr, "Buffer UAV '%s' (id=%llu) not found.", name.c_str(), id);
			bindStaticVarAllStages(name, pUAV);
		}
	}
} // namespace shz