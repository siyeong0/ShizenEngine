// ============================================================================
// Engine/Renderer/Public/MaterialRenderData.h
//   - Creates PSO from MaterialInstance (no fixed PSO input).
//   - Creates SRB and binds immediately using instance values/resources.
// ============================================================================
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
			IMaterialStaticBinder* pStaticBinder = nullptr);

		bool IsValid() const noexcept
		{
			return (m_pSRB != nullptr) && (m_pPSO != nullptr) && (m_pTemplate != nullptr);
		}

		IPipelineState* GetPSO() const noexcept
		{
			return m_pPSO;
		}
		IShaderResourceBinding* GetSRB() const noexcept
		{
			return m_pSRB;
		}

		IBuffer* GetMaterialConstantsBuffer() const noexcept
		{
			return m_pMaterialConstants;
		}
		const std::vector<Handle<TextureRenderData>>& GetBoundTextures() const noexcept
		{
			return m_BoundTextures;
		}

		// Re-apply per-instance values/resources (e.g., after SetFloat/SetTexture changes).
		bool Apply(RenderResourceCache* pCache, MaterialInstance& inst, IDeviceContext* pCtx);

	private:
		bool createPso(IRenderDevice* pDevice, const MaterialInstance& inst, IMaterialStaticBinder* pStaticBinder);
		bool createSrbAndBindMaterialCBuffer(IRenderDevice* pDevice, const MaterialInstance& inst);

		bool bindAllTextures(RenderResourceCache* pCache, MaterialInstance& inst);
		bool updateMaterialConstants(MaterialInstance& inst, IDeviceContext* pCtx);

		IShaderResourceVariable* findVarAnyStage(const char* name, const MaterialInstance& inst) const;

		uint32 findMaterialCBufferIndexFallback(const MaterialTemplate* pTemplate) const;

	private:
		RefCntAutoPtr<IPipelineState>          m_pPSO = {};
		RefCntAutoPtr<IShaderResourceBinding>  m_pSRB = {};
		RefCntAutoPtr<IBuffer>                m_pMaterialConstants = {};
		std::vector<Handle<TextureRenderData>> m_BoundTextures = {};

		const MaterialTemplate* m_pTemplate = nullptr;
		uint32 m_MaterialCBufferIndex = 0;

		uint64 m_LastConstantsUpdateFrame = 0xFFFFFFFFFFFFFFFFull;

	};
} // namespace shz
