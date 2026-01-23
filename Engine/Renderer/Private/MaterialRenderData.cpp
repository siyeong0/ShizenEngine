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
	uint32 MaterialRenderData::findMaterialCBufferIndexFallback() const
	{
		const uint32 cbCount = m_SourceInstance.GetTemplate()->GetCBufferCount();
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
			const auto& cb = m_SourceInstance.GetTemplate()->GetCBuffer(i);
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
		IMaterialStaticBinder* pStaticBinder,
		IPipelineState* pShadowPSO)
	{
		m_pPSO.Release();
		m_pSRB.Release();
		m_pShadowSRB.Release();
		m_pMaterialConstants.Release();
		m_BoundTextures.clear();

		m_SourceInstance = inst;
		if (!pDevice) { return false; }

		m_MaterialCBufferIndex = findMaterialCBufferIndexFallback();

		if (!createPso(pDevice, pStaticBinder)) { return false; }
		if (!createSrbAndBindMaterialCBuffer(pDevice)) { return false; }

		// Optional: shadow SRB (renderer-owned PSO).
		if (pShadowPSO != nullptr)
		{
			if (!createShadowSrbAndBindMaterialCBuffer(pShadowPSO)) { return false; }
		}

		// Immediate initial binding
		if (!Apply(pCache, pCtx)) { return false; }

		return true;
	}


	bool MaterialRenderData::createPso(IRenderDevice* pDevice, IMaterialStaticBinder* pStaticBinder)
	{
		ASSERT(pDevice, "Device is null.");

		const MATERIAL_PIPELINE_TYPE pt = m_SourceInstance.GetPipelineType();

		if (pt == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			GraphicsPipelineStateCreateInfo psoCi = {};
			psoCi.PSODesc = m_SourceInstance.GetPSODesc();

			// IMPORTANT: psoCi.PSODesc.Name points to string storage owned by MaterialInstance.
			// This must remain valid during CreateGraphicsPipelineState().
			psoCi.GraphicsPipeline = m_SourceInstance.GetGraphicsPipelineDesc();

			if (psoCi.GraphicsPipeline.pRenderPass == nullptr)
			{
				return false;
			}

			// Attach shaders from instance
			{
				bool hasMeshStages = false;
				bool hasLegacyStages = false;

				for (const RefCntAutoPtr<IShader>& s : m_SourceInstance.GetShaders())
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
				psoCi.PSODesc.ResourceLayout.DefaultVariableType = m_SourceInstance.GetDefaultVariableType();

				psoCi.PSODesc.ResourceLayout.Variables = m_SourceInstance.GetLayoutVarCount() > 0 ? m_SourceInstance.GetLayoutVars() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumVariables = m_SourceInstance.GetLayoutVarCount();

				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount() > 0 ? m_SourceInstance.GetImmutableSamplers() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount();
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
			psoCi.PSODesc = m_SourceInstance.GetPSODesc();

			for (const RefCntAutoPtr<IShader>& s : m_SourceInstance.GetShaders())
			{
				if (!s)
					continue;

				if (s->GetDesc().ShaderType == SHADER_TYPE_COMPUTE)
					psoCi.pCS = s.RawPtr();
			}

			{
				psoCi.PSODesc.ResourceLayout.DefaultVariableType = m_SourceInstance.GetDefaultVariableType();

				psoCi.PSODesc.ResourceLayout.Variables = m_SourceInstance.GetLayoutVarCount() > 0 ? m_SourceInstance.GetLayoutVars() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumVariables = m_SourceInstance.GetLayoutVarCount();

				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount() > 0 ? m_SourceInstance.GetImmutableSamplers() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount();
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


	bool MaterialRenderData::createSrbAndBindMaterialCBuffer(IRenderDevice* pDevice)
	{
		m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
		if (!m_pSRB)
		{
			return false;
		}

		// Create dynamic material constants buffer if template has cbuffers.
		const uint32 cbCount = m_SourceInstance.GetTemplate()->GetCBufferCount();
		if (cbCount > 0)
		{
			const auto& cb = m_SourceInstance.GetTemplate()->GetCBuffer(m_MaterialCBufferIndex);

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
				for (const RefCntAutoPtr<IShader>& s : m_SourceInstance.GetShaders())
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

	bool MaterialRenderData::createShadowSrbAndBindMaterialCBuffer(IPipelineState* pShadowPSO)
	{
		pShadowPSO->CreateShaderResourceBinding(&m_pShadowSRB, true);
		if (!m_pShadowSRB) { return false; }

		if (!m_pMaterialConstants) { return true; }

		// Bind material cbuffer by name for common stages used in shadow pass.
		{
			IShaderResourceVariable* v = nullptr;

			v = m_pShadowSRB->GetVariableByName(SHADER_TYPE_VERTEX, MaterialTemplate::MATERIAL_CBUFFER_NAME);
			if (v)
			{
				v->Set(m_pMaterialConstants);
			}

			v = m_pShadowSRB->GetVariableByName(SHADER_TYPE_PIXEL, MaterialTemplate::MATERIAL_CBUFFER_NAME);
			if (v)
			{
				v->Set(m_pMaterialConstants);
			}

			v = m_pShadowSRB->GetVariableByName(SHADER_TYPE_GEOMETRY, MaterialTemplate::MATERIAL_CBUFFER_NAME);
			if (v)
			{
				v->Set(m_pMaterialConstants);
			}
		}

		return true;
	}


	IShaderResourceVariable* MaterialRenderData::findVarAnyStage(const char* name) const
	{
		if (!m_pSRB || !name || name[0] == '\0')
		{
			return nullptr;
		}

		for (const RefCntAutoPtr<IShader>& s : m_SourceInstance.GetShaders())
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
	IShaderResourceVariable* MaterialRenderData::findVarShadowAnyStage(const char* name) const
	{
		if (!m_pShadowSRB || !name || name[0] == '\0') { return nullptr; }

		// Shadow pass usually uses VS(+PS for alpha test).
		IShaderResourceVariable* v = nullptr;

		v = m_pShadowSRB->GetVariableByName(SHADER_TYPE_PIXEL, name);
		if (v) { return v; }

		v = m_pShadowSRB->GetVariableByName(SHADER_TYPE_VERTEX, name);
		if (v) { return v; }

		v = m_pShadowSRB->GetVariableByName(SHADER_TYPE_GEOMETRY, name);
		if (v) { return v; }

		return nullptr;
	}

	bool MaterialRenderData::bindAllTexturesToShadow(RenderResourceCache* pCache)
	{
		const uint32 resCount = m_SourceInstance.GetTemplate()->GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& res = m_SourceInstance.GetTemplate()->GetResource(i);

			if (res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2D &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURECUBE) {
				continue;
			}

			// Use same dirty bit policy as main SRB.
			if (!m_SourceInstance.IsTextureDirty(i)) { continue; }

			const TextureBinding& b = m_SourceInstance.GetTextureBinding(i);

			ITextureView* pView = nullptr;

			if (b.TextureRef.has_value() && b.TextureRef->IsValid()) {
				const Handle<TextureRenderData> hTexRD = pCache->GetOrCreateTextureRenderData(*b.TextureRef);
				const TextureRenderData* texRD = pCache->TryGetTextureRenderData(hTexRD);
				if (texRD) { pView = texRD->GetSRV(); }
			}

			if (!pView) { pView = pCache->GetErrorTexture().GetSRV(); }

			IShaderResourceVariable* pVar = findVarShadowAnyStage(res.Name.c_str());
			if (pVar) { pVar->Set(pView); }

			// NOTE: do not clear dirty here; main SRB binder clears it.
		}

		return true;
	}


	bool MaterialRenderData::updateMaterialConstants(IDeviceContext* pCtx)
	{
		if (!m_pMaterialConstants || !pCtx)
		{
			return true;
		}

		const uint32 cbCount = m_SourceInstance.GetCBufferBlobCount();
		if (m_MaterialCBufferIndex >= cbCount)
		{
			return true;
		}

		const uint64 frameIndex = pCtx->GetFrameNumber();

		const bool bFirstUseThisFrame = (m_LastConstantsUpdateFrame != frameIndex);
		const bool bDirty = m_SourceInstance.IsCBufferDirty(m_MaterialCBufferIndex);

		if (!bFirstUseThisFrame && !bDirty)
		{
			return true;
		}

		const uint8* pBlob = m_SourceInstance.GetCBufferBlobData(m_MaterialCBufferIndex);
		const uint32 blobSize = m_SourceInstance.GetCBufferBlobSize(m_MaterialCBufferIndex);

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
			m_SourceInstance.ClearCBufferDirty(m_MaterialCBufferIndex);
		}

		return true;
	}



	bool MaterialRenderData::bindAllTextures(RenderResourceCache* pCache)
	{

		m_BoundTextures.clear();

		const uint32 resCount = m_SourceInstance.GetTemplate()->GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& res = m_SourceInstance.GetTemplate()->GetResource(i);

			if (res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2D &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY &&
				res.Type != MATERIAL_RESOURCE_TYPE_TEXTURECUBE)
			{
				continue;
			}

			if (!m_SourceInstance.IsTextureDirty(i))
			{
				continue;
			}

			const TextureBinding& b = m_SourceInstance.GetTextureBinding(i);

			ITextureView* pView = nullptr;

			// ------------------------------------------------------------
			// 1) If user provided a texture ref, bind it.
			// 2) Otherwise, bind a error texture for this slot.
			// ------------------------------------------------------------
			if (b.TextureRef.has_value() && b.TextureRef->IsValid())
			{
				const Handle<TextureRenderData> hTexRD = pCache->GetOrCreateTextureRenderData(*b.TextureRef);
				const TextureRenderData* texRD = pCache->TryGetTextureRenderData(hTexRD);

				if (texRD)
				{
					pView = texRD->GetSRV();
					m_BoundTextures.push_back(hTexRD);
				}
			}

			if (!pView)
			{
				// Fall back to error texture SRV (white/normalF/orm/black/cube etc.)
				pView = pCache->GetErrorTexture().GetSRV();
			}

			IShaderResourceVariable* pVar = findVarAnyStage(res.Name.c_str());
			if (pVar)
			{
				pVar->Set(pView);
			}

			m_SourceInstance.ClearTextureDirty(i);
		}

		return true;
	}

	bool MaterialRenderData::Apply(RenderResourceCache* pCache, IDeviceContext* pCtx)
	{
		if (!IsValid()) { return false; }

		if (!updateMaterialConstants(pCtx))
		{
			return false;
		}

		// Bind shadow textures first (because bindAllTextures clears dirty).
		if (m_pShadowSRB) {
			if (!bindAllTexturesToShadow(pCache)) { return false; }
		}

		if (!bindAllTextures(pCache)) { return false; }

		return true;
	}



} // namespace shz
