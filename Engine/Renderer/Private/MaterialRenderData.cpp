// ============================================================================
// Engine/Renderer/Private/MaterialRenderData.cpp
// ============================================================================
#include "pch.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"

#include <cstring>
#include <algorithm>

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	uint32 MaterialRenderData::findMaterialCBufferIndexFallback(const MaterialTemplate* pTemplate) const
	{
		if (!pTemplate)
		{
			return 0;
		}

		const uint32 cbCount = pTemplate->GetCBufferCount();
		if (cbCount == 0)
		{
			return 0;
		}
		if (cbCount == 1)
		{
			return 0;
		}

		for (uint32 i = 0; i < cbCount; ++i)
		{
			const auto& cb = pTemplate->GetCBuffer(i);
			if (cb.Name == MaterialTemplate::MATERIAL_CBUFFER_NAME)
			{
				return i;
			}
		}

		return 0;
	}

	bool MaterialRenderData::Initialize(
		IRenderDevice* pDevice,
		RenderResourceCache* pCache,
		IDeviceContext* pCtx,
		const MaterialInstance& inst,
		IMaterialStaticBinder* pStaticBinder)
	{
		m_pPSO.Release();
		m_pSRB.Release();
		m_pMaterialConstants.Release();
		m_BoundTextures.clear();

		m_pTemplate = inst.GetTemplate();
		if (!pDevice || !m_pTemplate)
		{
			return false;
		}

		m_MaterialCBufferIndex = findMaterialCBufferIndexFallback(m_pTemplate);

		if (!createPso(pDevice, inst, pStaticBinder))
		{
			return false;
		}

		if (!createSrbAndBindMaterialCBuffer(pDevice, inst))
		{
			return false;
		}

		// Immediate initial binding
		if (!Apply(pCache, inst, pCtx))
		{
			return false;
		}

		return true;
	}

	bool MaterialRenderData::createPso(IRenderDevice* pDevice, const MaterialInstance& inst, IMaterialStaticBinder* pStaticBinder)
	{
		ASSERT(pDevice, "Device is null.");

		const MATERIAL_PIPELINE_TYPE pt = inst.GetPipelineType();
		const MaterialResourceLayoutDesc& layout = inst.GetResourceLayout();

		if (pt == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			const MaterialGraphicsPsoDesc& mp = inst.GetGraphicsPsoDesc();

			GraphicsPipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc = mp.PSODesc;

			// IMPORTANT: PSODesc.Name must remain valid during Create* call.
			// We keep mp.Name as std::string storage inside MaterialInstance.
			psoCi.PSODesc.Name = mp.Name.c_str();

			psoCi.GraphicsPipeline = mp.GraphicsPipeline;

			// Attach shaders from instance
			for (const RefCntAutoPtr<IShader>& s : inst.GetShaders())
			{
				if (!s)
				{
					continue;
				}

				const SHADER_TYPE st = s->GetDesc().ShaderType;
				if (st == SHADER_TYPE_VERTEX)
				{
					psoCi.pVS = s.RawPtr();
				}
				else if (st == SHADER_TYPE_PIXEL)
				{
					psoCi.pPS = s.RawPtr();
				}
				else if (st == SHADER_TYPE_GEOMETRY)
				{
					psoCi.pGS = s.RawPtr();
				}
				else if (st == SHADER_TYPE_HULL)
				{
					psoCi.pHS = s.RawPtr();
				}
				else if (st == SHADER_TYPE_DOMAIN)
				{
					psoCi.pDS = s.RawPtr();
				}
				else if (st == SHADER_TYPE_AMPLIFICATION)
				{
					psoCi.pAS = s.RawPtr();
				}
				else if (st == SHADER_TYPE_MESH)
				{
					psoCi.pMS = s.RawPtr();
				}
			}

			psoCi.PSODesc.ResourceLayout.DefaultVariableType = layout.DefaultVariableType;

			psoCi.PSODesc.ResourceLayout.Variables = layout.Variables.empty() ? nullptr : layout.Variables.data();
			psoCi.PSODesc.ResourceLayout.NumVariables = static_cast<uint32>(layout.Variables.size());

			psoCi.PSODesc.ResourceLayout.ImmutableSamplers = layout.ImmutableSamplers.empty() ? nullptr : layout.ImmutableSamplers.data();
			psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<uint32>(layout.ImmutableSamplers.size());

			RefCntAutoPtr<IPipelineState> pPSO;
			pDevice->CreateGraphicsPipelineState(psoCi, &pPSO);

			if (!pPSO)
			{
				ASSERT(false, "Failed to create material graphics PSO.");
				return false;
			}

			m_pPSO = pPSO;

			if (pStaticBinder)
			{
				if (!pStaticBinder->BindStatics(m_pPSO))
				{
					false;
				}
			}

			return true;
		}
		else if (pt == MATERIAL_PIPELINE_TYPE_COMPUTE)
		{
			const MaterialComputePsoDesc& mp = inst.GetComputePsoDesc();

			ComputePipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc = mp.PSODesc;
			psoCi.PSODesc.Name = mp.Name.c_str();

			for (const RefCntAutoPtr<IShader>& s : inst.GetShaders())
			{
				if (!s)
				{
					continue;
				}

				const SHADER_TYPE st = s->GetDesc().ShaderType;
				if (st == SHADER_TYPE_COMPUTE)
				{
					psoCi.pCS = s.RawPtr();
				}
			}

			psoCi.PSODesc.ResourceLayout.DefaultVariableType = layout.DefaultVariableType;

			psoCi.PSODesc.ResourceLayout.Variables = layout.Variables.empty() ? nullptr : layout.Variables.data();
			psoCi.PSODesc.ResourceLayout.NumVariables = static_cast<uint32>(layout.Variables.size());

			psoCi.PSODesc.ResourceLayout.ImmutableSamplers = layout.ImmutableSamplers.empty() ? nullptr : layout.ImmutableSamplers.data();
			psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<uint32>(layout.ImmutableSamplers.size());

			RefCntAutoPtr<IPipelineState> pPSO;
			pDevice->CreateComputePipelineState(psoCi, &pPSO);

			if (!pPSO)
			{
				ASSERT(false, "Failed to create material compute PSO.");
				return false;
			}

			m_pPSO = pPSO;

			if (pStaticBinder)
			{
				if (!pStaticBinder->BindStatics(m_pPSO))
				{
					false;
				}
			}

			return true;
		}

		ASSERT(false, "Unsupported pipeline type.");
		return false;
	}

	bool MaterialRenderData::createSrbAndBindMaterialCBuffer(IRenderDevice* pDevice, const MaterialInstance& inst)
	{
		if (!m_pPSO || !m_pTemplate)
		{
			return false;
		}

		m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
		if (!m_pSRB)
		{
			return false;
		}

		// Create dynamic material constants buffer if template has cbuffers.
		const uint32 cbCount = m_pTemplate->GetCBufferCount();
		if (cbCount > 0)
		{
			const auto& cb = m_pTemplate->GetCBuffer(m_MaterialCBufferIndex);

			BufferDesc desc = {};
			desc.Name = "MaterialConstants";
			desc.Usage = USAGE_DYNAMIC;
			desc.BindFlags = BIND_UNIFORM_BUFFER;
			desc.CPUAccessFlags = CPU_ACCESS_WRITE;
			desc.Size = cb.ByteSize;

			RefCntAutoPtr<IBuffer> pBuf;
			pDevice->CreateBuffer(desc, nullptr, &pBuf);

			m_pMaterialConstants = pBuf;

			if (m_pMaterialConstants)
			{
				// Bind by name for all stages (missing variables are simply ignored).
				for (const RefCntAutoPtr<IShader>& s : inst.GetShaders())
				{
					if (!s)
					{
						continue;
					}

					const SHADER_TYPE st = s->GetDesc().ShaderType;
					IShaderResourceVariable* var = m_pSRB->GetVariableByName(st, MaterialTemplate::MATERIAL_CBUFFER_NAME);

					if (var)
					{
						var->Set(m_pMaterialConstants);
					}
				}
			}
		}

		return true;
	}

	IShaderResourceVariable* MaterialRenderData::findVarAnyStage(const char* name, const MaterialInstance& inst) const
	{
		if (!m_pSRB || !name || name[0] == '\0')
		{
			return nullptr;
		}

		for (const RefCntAutoPtr<IShader>& s : inst.GetShaders())
		{
			if (!s)
			{
				continue;
			}

			const SHADER_TYPE st = s->GetDesc().ShaderType;
			IShaderResourceVariable* v = m_pSRB->GetVariableByName(st, name);

			if (v)
			{
				return v;
			}
		}

		return nullptr;
	}

	bool MaterialRenderData::updateMaterialConstants(const MaterialInstance& inst, IDeviceContext* pCtx)
	{
		if (!m_pMaterialConstants || !pCtx)
		{
			return true;
		}

		const uint32 cbCount = inst.GetCBufferBlobCount();
		if (m_MaterialCBufferIndex >= cbCount)
		{
			return true;
		}

		if (!inst.IsCBufferDirty(m_MaterialCBufferIndex))
		{
			return true;
		}

		const uint8* pBlob = inst.GetCBufferBlobData(m_MaterialCBufferIndex);
		const uint32 blobSize = inst.GetCBufferBlobSize(m_MaterialCBufferIndex);

		if (!pBlob || blobSize == 0)
		{
			return false;
		}

		MapHelper<uint8> map(pCtx, m_pMaterialConstants, MAP_WRITE, MAP_FLAG_DISCARD);
		if (!map)
		{
			return false;
		}

		std::memcpy(map, pBlob, blobSize);
		return true;
	}

	bool MaterialRenderData::bindAllTextures(RenderResourceCache* pCache, const MaterialInstance& inst)
	{
		if (!m_pTemplate || !m_pSRB || !pCache)
		{
			return false;
		}

		m_BoundTextures.clear();

		const uint32 resCount = m_pTemplate->GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& res = m_pTemplate->GetResource(i);

			if (res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2D &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURECUBE)
			{
				continue;
			}

			const TextureBinding& b = inst.GetTextureBinding(i);

			ITextureView* pView = b.pRuntimeView;

			// Policy:
			// - runtime view wins
			// - otherwise resolve via cache using AssetRef
			if (pView == nullptr && b.TextureRef)
			{
				const Handle<TextureRenderData> hTexRD = pCache->GetOrCreateTextureRenderData(b.TextureRef);
				const TextureRenderData* texRD = pCache->TryGetTextureRenderData(hTexRD);

				if (texRD)
				{
					pView = texRD->GetSRV();
					m_BoundTextures.push_back(hTexRD);
				}
			}

			IShaderResourceVariable* pVar = findVarAnyStage(res.Name.c_str(), inst);
			if (pVar)
			{
				pVar->Set(pView);
			}
		}

		return true;
	}

	bool MaterialRenderData::Apply(RenderResourceCache* pCache, const MaterialInstance& inst, IDeviceContext* pCtx)
	{
		if (!IsValid())
		{
			return false;
		}

		if (!updateMaterialConstants(inst, pCtx))
		{
			return false;
		}

		if (!bindAllTextures(pCache, inst))
		{
			return false;
		}

		return true;
	}

} // namespace shz
