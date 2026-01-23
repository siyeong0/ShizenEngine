#pragma once
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

		bool Apply(RenderResourceCache* pCach, IDeviceContext* pCtx);

		IPipelineState* GetPSO() const noexcept { return m_pPSO; }
		IShaderResourceBinding* GetSRB() const noexcept { return m_pSRB; }

		IShaderResourceBinding* GetShadowSRB() const noexcept { return m_pShadowSRB; }

		IBuffer* GetMaterialConstantsBuffer() const noexcept { return m_pMaterialConstants; }
		const std::vector<Handle<TextureRenderData>>& GetBoundTextures() const noexcept { return m_BoundTextures; }

	private:
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
