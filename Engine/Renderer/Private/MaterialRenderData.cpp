#include "pch.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"

#include <cstring>

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	static uint32 findMaterialCBufferIndexFallback(const MaterialTemplate* pTemplate)
	{
		if (!pTemplate)
			return 0;

		const uint32 cbCount = pTemplate->GetCBufferCount();
		if (cbCount == 0)
			return 0;
		if (cbCount == 1)
			return 0;

		for (uint32 i = 0; i < cbCount; ++i)
		{
			const auto& cb = pTemplate->GetCBuffer(i);
			if (cb.Name == "MATERIAL_CONSTANTS")
				return i;
		}

		return 0;
	}

	bool MaterialRenderData::Initialize(IRenderDevice* pDevice, IPipelineState* pPSO, const MaterialTemplate* pTemplate)
	{
		m_pPSO = pPSO;
		m_pTemplate = pTemplate;

		m_pSRB.Release();
		m_pMaterialConstants.Release();

		if (!pDevice || !m_pPSO || !m_pTemplate)
			return false;

		m_MaterialCBufferIndex = findMaterialCBufferIndexFallback(m_pTemplate);

		m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
		if (!m_pSRB)
			return false;

		// Create dynamic material constants buffer if template has cbuffers
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

			// Bind cb var if shader variable exists
			if (m_pMaterialConstants)
			{
				if (auto* varPS = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "MATERIAL_CONSTANTS"))
					varPS->Set(m_pMaterialConstants);
				if (auto* varVS = m_pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "MATERIAL_CONSTANTS"))
					varVS->Set(m_pMaterialConstants);
			}
		}

		return true;
	}

	IShaderResourceVariable* MaterialRenderData::findVarAnyStage(const char* name) const
	{
		if (!m_pSRB || !name)
			return nullptr;

		if (auto* v = m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, name))
			return v;
		if (auto* v = m_pSRB->GetVariableByName(SHADER_TYPE_VERTEX, name))
			return v;

		return nullptr;
	}

	bool MaterialRenderData::updateMaterialConstants(const MaterialInstance& inst, IDeviceContext* pCtx)
	{
		if (!m_pMaterialConstants || !pCtx)
			return true;

		const uint32 cbCount = inst.GetCBufferBlobCount();
		if (m_MaterialCBufferIndex >= cbCount)
			return true;

		if (!inst.IsCBufferDirty(m_MaterialCBufferIndex))
			return true;

		const uint8* pBlob = inst.GetCBufferBlobData(m_MaterialCBufferIndex);
		const uint32 blobSize = inst.GetCBufferBlobSize(m_MaterialCBufferIndex);
		if (!pBlob || blobSize == 0)
			return false;

		MapHelper<uint8> map(pCtx, m_pMaterialConstants, MAP_WRITE, MAP_FLAG_DISCARD);
		if (!map)
			return false;

		std::memcpy(map, pBlob, blobSize);
		return true;
	}

	bool MaterialRenderData::bindAllTextures(RenderResourceCache* pCache, const MaterialInstance& inst)
	{
		if (!m_pTemplate || !m_pSRB || !pCache)
			return false;

		const uint32 resCount = m_pTemplate->GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const auto& res = m_pTemplate->GetResource(i);

			if (res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2D &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURECUBE)
			{
				continue;
			}

			const TextureBinding& b = inst.GetTextureBinding(i);

			ITextureView* pView = b.pRuntimeView;

			if (pView == nullptr && b.TextureHandle.IsValid())
			{
				const Handle<TextureRenderData> hTexRD = pCache->GetOrCreateTextureRenderData(b.TextureHandle);
				if (const TextureRenderData* texRD = pCache->TryGetTextureRenderData(hTexRD))
				{
					pView = texRD->GetSRV();
					m_BoundTextures.push_back(hTexRD);
				}
			}

			if (IShaderResourceVariable* pVar = findVarAnyStage(res.Name.c_str()))
			{
				pVar->Set(pView);
			}
		}

		return true;
	}

	bool MaterialRenderData::Apply(RenderResourceCache* pCache, const MaterialInstance& inst, IDeviceContext* pCtx)
	{
		if (!IsValid())
			return false;

		if (!updateMaterialConstants(inst, pCtx))
			return false;

		if (!bindAllTextures(pCache, inst))
			return false;

		return true;
	}
}
