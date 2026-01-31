#include "pch.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

namespace shz
{
	void RenderResourceRegistry::Initialize()
	{
		// Nothing for now. Kept for symmetry and future expansion.
	}

	void RenderResourceRegistry::Shutdown()
	{
		m_Textures.clear();
		m_Buffers.clear();
	}

	void RenderResourceRegistry::RegisterTexture(RenderResID id, RefCntAutoPtr<ITexture>&& pTexture)
	{
		ASSERT(id != 0, "RegisterTexture: id must be non-zero.");

		TextureEntry& e = m_Textures[id];

		// Replace owned texture
		e.OwnedTexture = std::move(pTexture);

		// Refresh cached default views
		e.OwnedSRV.Release();
		e.OwnedRTV.Release();
		e.OwnedDSV.Release();
		e.OwnedUAV.Release();

		if (e.OwnedTexture)
		{
			rebuildOwnedTextureDefaultViews(e);
		}
	}

	void RenderResourceRegistry::RegisterBuffer(RenderResID id, RefCntAutoPtr<IBuffer>&& pBuffer)
	{
		ASSERT(id != 0, "RegisterBuffer: id must be non-zero.");

		BufferEntry& e = m_Buffers[id];
		e.OwnedBuffer = std::move(pBuffer);
	}

	void RenderResourceRegistry::BindExternalTextureViews(
		RenderResID id,
		ITexture* pTex,
		ITextureView* pSRV,
		ITextureView* pRTV,
		ITextureView* pDSV,
		ITextureView* pUAV)
	{
		ASSERT(id != 0, "BindExternalTextureViews: id must be non-zero.");

		TextureEntry& e = m_Textures[id];

		// If all are null => clear external
		if (!pTex && !pSRV && !pRTV && !pDSV && !pUAV)
		{
			e.ClearExternal();
			return;
		}

		// Update only provided subset (keep previous if null)
		if (pTex) e.pExternalTexture = pTex;
		if (pSRV) e.pExternalSRV = pSRV;
		if (pRTV) e.pExternalRTV = pRTV;
		if (pDSV) e.pExternalDSV = pDSV;
		if (pUAV) e.pExternalUAV = pUAV;
	}

	void RenderResourceRegistry::BindExternalBuffer(RenderResID id, IBuffer* pBuf)
	{
		ASSERT(id != 0, "BindExternalBuffer: id must be non-zero.");

		BufferEntry& e = m_Buffers[id];
		e.pExternalBuffer = pBuf; // can be null to clear
	}

	ITexture* RenderResourceRegistry::GetTexture(RenderResID id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "GetTexture: texture id not found.");

		const TextureEntry& e = it->second;

		if (e.OwnedTexture)
		{
			return e.OwnedTexture.RawPtr();
		}

		return e.pExternalTexture;
	}

	IBuffer* RenderResourceRegistry::GetBuffer(RenderResID id) const
	{
		auto it = m_Buffers.find(id);
		ASSERT(it != m_Buffers.end(), "GetBuffer: buffer id not found.");

		const BufferEntry& e = it->second;

		if (e.OwnedBuffer)
		{
			return e.OwnedBuffer.RawPtr();
		}

		return e.pExternalBuffer;
	}

	ITextureView* RenderResourceRegistry::GetSRV(RenderResID id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "GetSRV: texture id not found.");

		const TextureEntry& e = it->second;

		if (e.OwnedSRV)
		{
			return e.OwnedSRV.RawPtr();
		}

		return e.pExternalSRV;
	}

	ITextureView* RenderResourceRegistry::GetRTV(RenderResID id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "GetRTV: texture id not found.");

		const TextureEntry& e = it->second;

		if (e.OwnedRTV)
		{
			return e.OwnedRTV.RawPtr();
		}

		return e.pExternalRTV;
	}

	ITextureView* RenderResourceRegistry::GetDSV(RenderResID id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "GetRTV: texture id not found.");

		const TextureEntry& e = it->second;

		if (e.OwnedDSV)
		{
			return e.OwnedDSV.RawPtr();
		}

		return e.pExternalDSV;
	}

	ITextureView* RenderResourceRegistry::GetUAV(RenderResID id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "GetRTV: texture id not found.");

		const TextureEntry& e = it->second;

		if (e.OwnedUAV)
		{
			return e.OwnedUAV.RawPtr();
		}

		return e.pExternalUAV;
	}

	void RenderResourceRegistry::UnregisterTexture(RenderResID id)
	{
		auto it = m_Textures.find(id);
		if (it == m_Textures.end())
			return;

		// Remove the entire entry (owned+external)
		m_Textures.erase(it);
	}

	void RenderResourceRegistry::UnregisterBuffer(RenderResID id)
	{
		auto it = m_Buffers.find(id);
		if (it == m_Buffers.end())
			return;

		m_Buffers.erase(it);
	}

	void RenderResourceRegistry::UnbindExternal(RenderResID id)
	{
		{
			auto itT = m_Textures.find(id);
			if (itT != m_Textures.end())
			{
				itT->second.ClearExternal();
			}
		}

		{
			auto itB = m_Buffers.find(id);
			if (itB != m_Buffers.end())
			{
				itB->second.ClearExternal();
			}
		}
	}

	void RenderResourceRegistry::rebuildOwnedTextureDefaultViews(TextureEntry& e)
	{
		ASSERT(e.OwnedTexture, "rebuildOwnedTextureDefaultViews: owned texture is null.");

		// Cache default views if available. Some textures may not have certain views depending on BindFlags.
		if (auto* srv = e.OwnedTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
			e.OwnedSRV = srv;

		if (auto* rtv = e.OwnedTexture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET))
			e.OwnedRTV = rtv;

		if (auto* dsv = e.OwnedTexture->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL))
			e.OwnedDSV = dsv;

		if (auto* uav = e.OwnedTexture->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS))
			e.OwnedUAV = uav;
	}
} // namespace shz
