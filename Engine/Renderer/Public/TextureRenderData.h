#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/ISampler.h"

namespace shz
{
	class TextureRenderData final
	{
	public:
		TextureRenderData() = default;
		~TextureRenderData() = default;

		bool IsValid() const noexcept
		{
			return (m_pTexture != nullptr);
		}

		ITexture* GetTexture() const noexcept { return m_pTexture; }
		ITextureView* GetSRV(TEXTURE_VIEW_TYPE type = TEXTURE_VIEW_SHADER_RESOURCE) const noexcept { return m_pTexture->GetDefaultView(type); }

		ISampler* GetDefaultSampler() const noexcept { return m_pDefaultSampler; }
		void SetDefaultSampler(ISampler* pSampler) noexcept { m_pDefaultSampler = pSampler; }

	public:
		void SetTexture(ITexture* pTex) noexcept { m_pTexture = pTex; }

	private:
		RefCntAutoPtr<ITexture> m_pTexture = {};
		RefCntAutoPtr<ISampler> m_pDefaultSampler = {};
	};
}
