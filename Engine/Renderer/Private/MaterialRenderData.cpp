#include "pch.h"
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"

#include <cstring>
#include <algorithm>

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{

	bool MaterialRenderData::Initialize(
		IRenderDevice* pDevice,
		RenderResourceCache* pCache,
		IDeviceContext* pCtx,
		MaterialInstance& inst,
		IMaterialStaticBinder* pStaticBinder,
		IPipelineState* pShadowPSO)
	{
		ASSERT(pDevice, "Device is null.");

		m_pPSO.Release();
		m_pSRB.Release();
		m_pShadowSRB.Release();
		m_pMaterialConstants.Release();
		m_BoundTextures.clear();

		m_SourceInstance = inst;

		m_MaterialCBufferIndex = findMaterialCBufferIndexFallback();

		// Create PSO
		{
			const MATERIAL_PIPELINE_TYPE pipelineType = m_SourceInstance.GetPipelineType();

			if (pipelineType == MATERIAL_PIPELINE_TYPE_GRAPHICS)
			{
				GraphicsPipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc = m_SourceInstance.GetPSODesc();
				psoCi.GraphicsPipeline = m_SourceInstance.GetGraphicsPipelineDesc();

				ASSERT(psoCi.GraphicsPipeline.pRenderPass != nullptr, "Render pass is null.");

				// Attach shaders from instance
				bool hasMeshStages = false;
				bool hasLegacyStages = false;

				for (const RefCntAutoPtr<IShader>& shader : m_SourceInstance.GetShaders())
				{
					ASSERT(shader, "Shader in source instance is null.");

					const SHADER_TYPE shaderType = shader->GetDesc().ShaderType;

					// classify for earlier diagnostic
					if (shaderType == SHADER_TYPE_MESH || shaderType == SHADER_TYPE_AMPLIFICATION)
					{
						hasMeshStages = true;
					}

					if (shaderType == SHADER_TYPE_VERTEX ||
						shaderType == SHADER_TYPE_GEOMETRY ||
						shaderType == SHADER_TYPE_HULL ||
						shaderType == SHADER_TYPE_DOMAIN)
					{
						hasLegacyStages = true;
					}

					if (shaderType == SHADER_TYPE_VERTEX)             psoCi.pVS = shader.RawPtr();
					else if (shaderType == SHADER_TYPE_PIXEL)         psoCi.pPS = shader.RawPtr();
					else if (shaderType == SHADER_TYPE_GEOMETRY)      psoCi.pGS = shader.RawPtr();
					else if (shaderType == SHADER_TYPE_HULL)          psoCi.pHS = shader.RawPtr();
					else if (shaderType == SHADER_TYPE_DOMAIN)        psoCi.pDS = shader.RawPtr();
					else if (shaderType == SHADER_TYPE_AMPLIFICATION) psoCi.pAS = shader.RawPtr();
					else if (shaderType == SHADER_TYPE_MESH)          psoCi.pMS = shader.RawPtr();
				}

				// Diligent/your utils assert: mesh stages can't be combined with legacy stages
				ASSERT(!(hasMeshStages && hasLegacyStages), "Invalid shader stage mix: mesh stages can't be combined with VS/GS/HS/DS.");

				// Auto-generated resource layout from instance
				psoCi.PSODesc.ResourceLayout.DefaultVariableType = m_SourceInstance.GetDefaultVariableType();

				psoCi.PSODesc.ResourceLayout.Variables = m_SourceInstance.GetLayoutVarCount() > 0 ? m_SourceInstance.GetLayoutVars() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumVariables = m_SourceInstance.GetLayoutVarCount();

				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount() > 0 ? m_SourceInstance.GetImmutableSamplers() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount();

				RefCntAutoPtr<IPipelineState> pPSO;
				pDevice->CreateGraphicsPipelineState(psoCi, &pPSO);

				ASSERT(pPSO, "Failed to create PSO.");
				m_pPSO = pPSO;

				if (pStaticBinder)
				{
					bool ok = pStaticBinder->BindStatics(m_pPSO);
					ASSERT(ok, "Failed to bind statics.");
				}
			}
			else if (pipelineType == MATERIAL_PIPELINE_TYPE_COMPUTE)
			{
				ComputePipelineStateCreateInfo psoCi = {};
				psoCi.PSODesc = m_SourceInstance.GetPSODesc();

				for (const RefCntAutoPtr<IShader>& shader : m_SourceInstance.GetShaders())
				{
					ASSERT(shader, "Shader in source instance is null.");
					ASSERT(shader->GetDesc().ShaderType == SHADER_TYPE_COMPUTE, "Shader type is not compute in compute pipeline.");
					psoCi.pCS = shader.RawPtr();
				}

				psoCi.PSODesc.ResourceLayout.DefaultVariableType = m_SourceInstance.GetDefaultVariableType();

				psoCi.PSODesc.ResourceLayout.Variables = m_SourceInstance.GetLayoutVarCount() > 0 ? m_SourceInstance.GetLayoutVars() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumVariables = m_SourceInstance.GetLayoutVarCount();

				psoCi.PSODesc.ResourceLayout.ImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount() > 0 ? m_SourceInstance.GetImmutableSamplers() : nullptr;
				psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = m_SourceInstance.GetImmutableSamplerCount();

				RefCntAutoPtr<IPipelineState> pPSO;
				pDevice->CreateComputePipelineState(psoCi, &pPSO);

				ASSERT(pPSO, "Failed to create PSO.");
				m_pPSO = pPSO;

				if (pStaticBinder)
				{
					bool ok = pStaticBinder->BindStatics(m_pPSO);
					ASSERT(ok, "Failed to bind statics.");
				}
			}
			else
			{
				ASSERT(false, "Unsupported pipeline type.");
				return false;
			}
		}

		// Create SRB and bind material CB
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
					for (const RefCntAutoPtr<IShader>& shader : m_SourceInstance.GetShaders())
					{
						ASSERT(shader, "Shader in source instance is null.");

						const SHADER_TYPE st = shader->GetDesc().ShaderType;

						IShaderResourceVariable* var = m_pSRB->GetVariableByName(st, MaterialTemplate::MATERIAL_CBUFFER_NAME);
						if (var)
						{
							var->Set(m_pMaterialConstants);
						}
					}
				}
			}
		}
		// Optional: shadow SRB (renderer-owned PSO).
		if (pShadowPSO != nullptr)
		{
			pShadowPSO->CreateShaderResourceBinding(&m_pShadowSRB, true);
			if (!m_pShadowSRB) { return false; }

			if (!m_pMaterialConstants) { return true; }

			// Bind material cbuffer by name for common stages used in shadow pass.
			{
				IShaderResourceVariable* v = nullptr;

				if (IShaderResourceVariable* var = m_pShadowSRB->GetVariableByName(SHADER_TYPE_VERTEX, MaterialTemplate::MATERIAL_CBUFFER_NAME))
				{
					var->Set(m_pMaterialConstants);
				}

				if (IShaderResourceVariable* var = m_pShadowSRB->GetVariableByName(SHADER_TYPE_PIXEL, MaterialTemplate::MATERIAL_CBUFFER_NAME))
				{
					var->Set(m_pMaterialConstants);
				}
			}
		}

		// Immediate initial binding
		bool ok = Apply(pCache, pCtx);
		ASSERT(ok, "Failed to apply bindings.");

		return true;
	}

	bool MaterialRenderData::Apply(RenderResourceCache* pCache, IDeviceContext* pCtx)
	{
		ASSERT(pCtx, "Context is null.");
		// Update material constants
		{
			if (!m_pMaterialConstants)
			{
				return true;
			}

			const uint32 cbCount = m_SourceInstance.GetCBufferBlobCount();
			ASSERT(m_MaterialCBufferIndex < cbCount, "CB index out of bounds.");

			const uint64 frameIndex = pCtx->GetFrameNumber();

			const bool bFirstUseThisFrame = (m_LastConstantsUpdateFrame != frameIndex);
			const bool bDirty = m_SourceInstance.IsCBufferDirty(m_MaterialCBufferIndex);

			if (!bFirstUseThisFrame && !bDirty)
			{
				return true;
			}

			const uint8* pBlob = m_SourceInstance.GetCBufferBlobData(m_MaterialCBufferIndex);
			const uint32 blobSize = m_SourceInstance.GetCBufferBlobSize(m_MaterialCBufferIndex);
			ASSERT(pBlob && blobSize > 0, "Invalid blob data.");

			MapHelper<uint8> map(pCtx, m_pMaterialConstants, MAP_WRITE, MAP_FLAG_DISCARD);
			ASSERT(map, "Failed to create CB mapper.");

			std::memcpy(map, pBlob, blobSize);

			m_LastConstantsUpdateFrame = frameIndex;

			if (bDirty)
			{
				m_SourceInstance.ClearCBufferDirty(m_MaterialCBufferIndex);
			}
		}

		// Bind shadow textures first (because bindAllTextures clears dirty).
		if (m_pShadowSRB)
		{
			const uint32 resCount = m_SourceInstance.GetTemplate()->GetResourceCount();
			for (uint32 i = 0; i < resCount; ++i)
			{
				const MaterialResourceDesc& resDesc = m_SourceInstance.GetTemplate()->GetResource(i);

				if (resDesc.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2D &&
					resDesc.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY &&
					resDesc.Type != MATERIAL_RESOURCE_TYPE_TEXTURECUBE)
				{
					continue;
				}

				// Use same dirty bit policy as main SRB.
				if (!m_SourceInstance.IsTextureDirty(i))
				{
					continue;
				}

				const TextureBinding& b = m_SourceInstance.GetTextureBinding(i);

				ITextureView* pView = nullptr;

				if (b.TextureRef.has_value() && b.TextureRef->IsValid())
				{
					const Handle<TextureRenderData> hTexRD = pCache->GetOrCreateTextureRenderData(*b.TextureRef);
					const TextureRenderData* texRD = pCache->TryGetTextureRenderData(hTexRD);
					ASSERT(texRD, "Texture render data is null.");
					pView = texRD->GetSRV();
				}
				else
				{
					pView = pCache->GetErrorTexture().GetSRV();
				}

				if (IShaderResourceVariable* pVar = findVarShadowAnyStage(resDesc.Name.c_str()))
				{
					pVar->Set(pView);
				}

				// NOTE: do not clear dirty here; main SRB binder clears it.
			}
		}

		if (!bindAllTextures(pCache)) { return false; }

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

	uint32 MaterialRenderData::findMaterialCBufferIndexFallback() const
	{
		for (uint32 i = 0; i < m_SourceInstance.GetTemplate()->GetCBufferCount(); ++i)
		{
			const auto& cb = m_SourceInstance.GetTemplate()->GetCBuffer(i);
			if (cb.Name == MaterialTemplate::MATERIAL_CBUFFER_NAME)
			{
				return i;
			}
		}

		return 0;
	}

} // namespace shz
