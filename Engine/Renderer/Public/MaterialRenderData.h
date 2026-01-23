// Engine/Renderer/Public/MaterialRenderData.h
#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"
#include "Engine/RHI/Interface/IShaderResourceVariable.h"
#include "Engine/RHI/Interface/IDeviceContext.h"

#include "Engine/Material/Public/MaterialInstance.h"

#include "Engine/Renderer/Public/TextureRenderData.h"
#include "Engine/Renderer/Public/IMaterialStaticBinder.h"

namespace shz
{
	class RenderResourceCache;

	class MaterialRenderData final
	{
	public:
		MaterialRenderData() = default;
		~MaterialRenderData() = default;

		// Creates PSO + SRB + material constants buffer and performs initial binding immediately.
		bool Initialize(
			IRenderDevice* pDevice,
			RenderResourceCache* pCache,
			IDeviceContext* pCtx,
			MaterialInstance& inst,
			IMaterialStaticBinder* pStaticBinder = nullptr,
			IPipelineState* pShadowPSO = nullptr);

		bool IsValid() const noexcept
		{
			return (m_pSRB != nullptr) && (m_pPSO != nullptr) && (m_SourceInstance.GetTemplate() != nullptr);
		}

		IPipelineState* GetPSO() const noexcept { return m_pPSO; }
		IShaderResourceBinding* GetSRB() const noexcept { return m_pSRB; }

		IShaderResourceBinding* GetShadowSRB() const noexcept { return m_pShadowSRB; }

		IBuffer* GetMaterialConstantsBuffer() const noexcept { return m_pMaterialConstants; }
		const std::vector<Handle<TextureRenderData>>& GetBoundTextures() const noexcept { return m_BoundTextures; }

		// Re-apply per-instance values/resources (e.g., after SetFloat/SetTexture changes).
		bool Apply(RenderResourceCache* pCach, IDeviceContext* pCtx);

	private:
		bool createPso(IRenderDevice* pDevice, IMaterialStaticBinder* pStaticBinder);
		bool createSrbAndBindMaterialCBuffer(IRenderDevice* pDevice);

		// Shadow SRB (optional): created from renderer-owned shadow PSO.
		bool createShadowSrbAndBindMaterialCBuffer(IPipelineState* pShadowPSO);

		bool bindAllTextures(RenderResourceCache* pCache);
		bool bindAllTexturesToShadow(RenderResourceCache* pCache);

		bool updateMaterialConstants(IDeviceContext* pCtx);

		IShaderResourceVariable* findVarAnyStage(const char* name) const;
		IShaderResourceVariable* findVarShadowAnyStage(const char* name) const;

		uint32 findMaterialCBufferIndexFallback() const;

	private:
		RefCntAutoPtr<IPipelineState>          m_pPSO = {};
		RefCntAutoPtr<IShaderResourceBinding>  m_pSRB = {};
		RefCntAutoPtr<IBuffer>                m_pMaterialConstants = {};
		std::vector<Handle<TextureRenderData>> m_BoundTextures = {};

		RefCntAutoPtr<IShaderResourceBinding>  m_pShadowSRB = {};

		MaterialInstance m_SourceInstance = {};
		uint32 m_MaterialCBufferIndex = 0;

		uint64 m_LastConstantsUpdateFrame = 0xFFFFFFFFFFFFFFFFull;
	};
} // namespace shz
