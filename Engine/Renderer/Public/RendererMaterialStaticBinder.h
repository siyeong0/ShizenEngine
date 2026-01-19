#pragma once
#include "Engine/Renderer/Public/IMaterialStaticBinder.h"

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IBufferView.h"

namespace shz
{
	// ----------------------------------------------------------------------------
	// RendererMaterialStaticBinder
	//
	// A small binder that knows about renderer-global static bindings used by
	// mesh material PSOs (e.g. GBuffer PSOs).
	// ----------------------------------------------------------------------------
	class RendererMaterialStaticBinder final : public IMaterialStaticBinder
	{
	public:
		RendererMaterialStaticBinder() = default;
		~RendererMaterialStaticBinder() override = default;

		RendererMaterialStaticBinder(const RendererMaterialStaticBinder&) = delete;
		RendererMaterialStaticBinder& operator=(const RendererMaterialStaticBinder&) = delete;

		void SetFrameConstants(IBuffer* pFrameCB) noexcept { m_pFrameCB = pFrameCB; }
		void SetObjectTableSRV(IBufferView* pObjectTableSRV) noexcept { m_pObjectTableSRV = pObjectTableSRV; }
		void SetLinearWrapSampler(ISampler* pSampler) noexcept { m_pLinearWrapSampler = pSampler; }

		bool BindStatics(IPipelineState* pPSO) override;

	private:
		RefCntAutoPtr<IBuffer>     m_pFrameCB = {};
		RefCntAutoPtr<IBufferView> m_pObjectTableSRV = {};
		RefCntAutoPtr<ISampler>    m_pLinearWrapSampler = {};
	};

} // namespace shz
