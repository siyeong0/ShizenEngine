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

#include "Engine/Material/Public/MaterialTemplate.h"
#include "Engine/Material/Public/MaterialInstance.h"

#include "Engine/Renderer/Public/TextureRenderData.h"

namespace shz
{
	class RenderResourceCache;

	class MaterialRenderData final
	{
	public:
		MaterialRenderData() = default;
		~MaterialRenderData() = default;

		bool Initialize(IRenderDevice* pDevice, IPipelineState* pPSO, const MaterialTemplate* pTemplate);

		bool IsValid() const noexcept
		{
			return (m_pSRB != nullptr) && (m_pPSO != nullptr) && (m_pTemplate != nullptr);
		}

		IPipelineState* GetPSO() const noexcept { return m_pPSO; }
		IShaderResourceBinding* GetSRB() const noexcept { return m_pSRB; }

		IBuffer* GetMaterialConstantsBuffer() const noexcept { return m_pMaterialConstants; }
		const std::vector<Handle<TextureRenderData>>& GetBoundTextures() const noexcept { return m_BoundTextures; }

		bool Apply(RenderResourceCache* pCache, const MaterialInstance& inst, IDeviceContext* pCtx);

	private:
		bool bindAllTextures(RenderResourceCache* pCache, const MaterialInstance& inst);
		bool updateMaterialConstants(const MaterialInstance& inst, IDeviceContext* pCtx);

		IShaderResourceVariable* findVarAnyStage(const char* name) const;

	private:
		RefCntAutoPtr<IPipelineState>          m_pPSO = {};
		RefCntAutoPtr<IShaderResourceBinding>  m_pSRB = {};
		RefCntAutoPtr<IBuffer>                m_pMaterialConstants = {};
		std::vector<Handle<TextureRenderData>> m_BoundTextures = {};

		const MaterialTemplate* m_pTemplate = nullptr;
		uint32 m_MaterialCBufferIndex = 0;
	};
}
