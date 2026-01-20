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
		MaterialInstance& inst,
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

	bool MaterialRenderData::createPso(
		IRenderDevice* pDevice,
		const MaterialInstance& inst,
		IMaterialStaticBinder* pStaticBinder)
	{
		ASSERT(pDevice, "Device is null.");

		const MATERIAL_PIPELINE_TYPE pt = inst.GetPipelineType();

		if (pt == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			GraphicsPipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc = inst.GetPSODesc();

			// IMPORTANT: psoCi.PSODesc.Name points to string storage owned by MaterialInstance.
			// This must remain valid during CreateGraphicsPipelineState().
			psoCi.GraphicsPipeline = inst.GetGraphicsPipelineDesc();

			if (psoCi.GraphicsPipeline.pRenderPass == nullptr)
			{
				return false;
			}

			// Attach shaders from instance
			{
				bool hasMeshStages = false;
				bool hasLegacyStages = false;

				for (const RefCntAutoPtr<IShader>& s : inst.GetShaders())
				{
					if (!s)
						continue;

					const SHADER_TYPE st = s->GetDesc().ShaderType;

					// classify for earlier diagnostic
					if (st == SHADER_TYPE_MESH || st == SHADER_TYPE_AMPLIFICATION)
						hasMeshStages = true;

					if (st == SHADER_TYPE_VERTEX || st == SHADER_TYPE_GEOMETRY || st == SHADER_TYPE_HULL || st == SHADER_TYPE_DOMAIN)
						hasLegacyStages = true;

					if (st == SHADER_TYPE_VERTEX)             psoCi.pVS = s.RawPtr();
					else if (st == SHADER_TYPE_PIXEL)         psoCi.pPS = s.RawPtr();
					else if (st == SHADER_TYPE_GEOMETRY)      psoCi.pGS = s.RawPtr();
					else if (st == SHADER_TYPE_HULL)          psoCi.pHS = s.RawPtr();
					else if (st == SHADER_TYPE_DOMAIN)        psoCi.pDS = s.RawPtr();
					else if (st == SHADER_TYPE_AMPLIFICATION) psoCi.pAS = s.RawPtr();
					else if (st == SHADER_TYPE_MESH)          psoCi.pMS = s.RawPtr();
				}

				// Diligent/your utils assert: mesh stages can't be combined with legacy stages
				ASSERT(!(hasMeshStages && hasLegacyStages), "Invalid shader stage mix: mesh stages can't be combined with VS/GS/HS/DS.");
			}

			// Auto-generated resource layout from instance
			{
				psoCi.PSODesc.ResourceLayout.DefaultVariableType = inst.GetDefaultVariableType();

				psoCi.PSODesc.ResourceLayout.Variables = inst.GetLayoutVarCount() > 0 ? inst.GetLayoutVars() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumVariables = inst.GetLayoutVarCount();

				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = inst.GetImmutableSamplerCount() > 0 ? inst.GetImmutableSamplers() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = inst.GetImmutableSamplerCount();
			}

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
					ASSERT(false, "IMaterialStaticBinder::BindStatics failed.");
					return false;
				}
			}

			return true;
		}
		else if (pt == MATERIAL_PIPELINE_TYPE_COMPUTE)
		{
			ComputePipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc = inst.GetPSODesc();

			for (const RefCntAutoPtr<IShader>& s : inst.GetShaders())
			{
				if (!s)
					continue;

				if (s->GetDesc().ShaderType == SHADER_TYPE_COMPUTE)
					psoCi.pCS = s.RawPtr();
			}

			{
				psoCi.PSODesc.ResourceLayout.DefaultVariableType = inst.GetDefaultVariableType();

				psoCi.PSODesc.ResourceLayout.Variables = inst.GetLayoutVarCount() > 0 ? inst.GetLayoutVars() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumVariables = inst.GetLayoutVarCount();

				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = inst.GetImmutableSamplerCount() > 0 ? inst.GetImmutableSamplers() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = inst.GetImmutableSamplerCount();
			}

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
					ASSERT(false, "IMaterialStaticBinder::BindStatics failed.");
					return false;
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
				// Bind by name for first stage that exposes it.
				for (const RefCntAutoPtr<IShader>& s : inst.GetShaders())
				{
					if (!s)
						continue;

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

	bool MaterialRenderData::updateMaterialConstants(MaterialInstance& inst, IDeviceContext* pCtx)
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

		const uint64 frameIndex = pCtx->GetFrameNumber();

		const bool bFirstUseThisFrame = (m_LastConstantsUpdateFrame != frameIndex);
		const bool bDirty = inst.IsCBufferDirty(m_MaterialCBufferIndex);

		if (!bFirstUseThisFrame && !bDirty)
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

		m_LastConstantsUpdateFrame = frameIndex;

		if (bDirty)
		{
			inst.ClearCBufferDirty(m_MaterialCBufferIndex);
		}

		return true;
	}



	bool MaterialRenderData::bindAllTextures(RenderResourceCache* pCache, MaterialInstance& inst)
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

			if (!inst.IsTextureDirty(i))
				continue;

			const TextureBinding& b = inst.GetTextureBinding(i);

			ASSERT(b.TextureRef, "Texture is not set.");
			ITextureView* pView = nullptr;

			const Handle<TextureRenderData> hTexRD = pCache->GetOrCreateTextureRenderData(b.TextureRef);
			const TextureRenderData* texRD = pCache->TryGetTextureRenderData(hTexRD);

			if (texRD)
			{
				pView = texRD->GetSRV();
				m_BoundTextures.push_back(hTexRD);
			}

			IShaderResourceVariable* pVar = findVarAnyStage(res.Name.c_str(), inst);
			if (pVar)
			{
				pVar->Set(pView);
			}

			inst.ClearTextureDirty(i);
		}

		return true;
	}


	bool MaterialRenderData::Apply(RenderResourceCache* pCache, MaterialInstance& inst, IDeviceContext* pCtx)
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
