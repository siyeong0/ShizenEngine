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

	void RenderResourceRegistry::RegisterTexture(RenderResourceId id, RefCntAutoPtr<ITexture>&& pTexture)
	{
		ASSERT(id != 0, "Id must be non-zero.");

		TextureEntry& e = m_Textures[id];

		// Replace owned texture
		e.Texture = std::move(pTexture);

		// Refresh cached default views
		e.SRV = e.Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		e.RTV = e.Texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		e.DSV = e.Texture->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
		e.UAV = e.Texture->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS);
	}

	void RenderResourceRegistry::RegisterBuffer(RenderResourceId id, RefCntAutoPtr<IBuffer>&& pBuffer)
	{
		ASSERT(id != 0, "Id must be non-zero.");

		BufferEntry& e = m_Buffers[id];
		e.Buffer = std::move(pBuffer);
		e.SRV = e.Buffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
		e.UAV = e.Buffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS);
	}

	void RenderResourceRegistry::CreateTextureView(RenderResourceId id, const TextureViewDesc& desc)
	{
		ASSERT(id != 0, "Id must be non-zero.");

		TextureEntry& e = m_Textures[id];
		switch (desc.ViewType)
		{
		case TEXTURE_VIEW_SHADER_RESOURCE:
			e.SRV.Release();
			e.Texture->CreateView(desc, &e.SRV);
			break;
		case TEXTURE_VIEW_RENDER_TARGET:
			e.RTV.Release();
			e.Texture->CreateView(desc, &e.RTV);
			break;
		case TEXTURE_VIEW_DEPTH_STENCIL:
			e.DSV.Release();
			e.Texture->CreateView(desc, &e.DSV);
			break;
		case TEXTURE_VIEW_UNORDERED_ACCESS:
			e.UAV.Release();
			e.Texture->CreateView(desc, &e.UAV);
			break;
		default:
			ASSERT(false, "Unsupported texture view type.");
			break;
		}
	}

	void RenderResourceRegistry::CreateBufferView(RenderResourceId id, const BufferViewDesc& desc)
	{
		ASSERT(id != 0, "Id must be non-zero.");

		BufferEntry& e = m_Buffers[id];
		switch (desc.ViewType)
		{
		case BUFFER_VIEW_SHADER_RESOURCE:
			e.SRV.Release();
			e.Buffer->CreateView(desc, &e.SRV);
			break;
		case BUFFER_VIEW_UNORDERED_ACCESS:
			e.UAV.Release();
			e.Buffer->CreateView(desc, &e.UAV);
			break;
		default:
			ASSERT(false, "Unsupported buffer view type.");
			break;
		}
	}

	ITexture* RenderResourceRegistry::GetTexture(RenderResourceId id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "Texture id not found.");

		const TextureEntry& e = it->second;
		return e.Texture;
	}

	IBuffer* RenderResourceRegistry::GetBuffer(RenderResourceId id) const
	{
		auto it = m_Buffers.find(id);
		ASSERT(it != m_Buffers.end(), "Buffer id not found.");

		const BufferEntry& e = it->second;
		return e.Buffer;
	}

	ITextureView* RenderResourceRegistry::GetTextureSRV(RenderResourceId id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "Texture id not found.");

		const TextureEntry& e = it->second;
		return e.SRV;
	}

	ITextureView* RenderResourceRegistry::GetTextureRTV(RenderResourceId id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "Texture id not found.");

		const TextureEntry& e = it->second;
		return e.RTV;
	}

	ITextureView* RenderResourceRegistry::GetTextureDSV(RenderResourceId id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "Texture id not found.");

		const TextureEntry& e = it->second;
		return e.DSV;
	}

	ITextureView* RenderResourceRegistry::GetTextureUAV(RenderResourceId id) const
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "Texture id not found.");

		const TextureEntry& e = it->second;
		return e.UAV;
	}

	IBufferView* RenderResourceRegistry::GetBufferSRV(RenderResourceId id) const
	{
		auto it = m_Buffers.find(id);
		ASSERT(it != m_Buffers.end(), "Buffer id not found.");

		const BufferEntry& e = it->second;
		return e.SRV;
	}

	IBufferView* RenderResourceRegistry::GetBufferUAV(RenderResourceId id) const
	{
		auto it = m_Buffers.find(id);
		ASSERT(it != m_Buffers.end(), "Buffer id not found.");

		const BufferEntry& e = it->second;
		return e.UAV;
	}

	void RenderResourceRegistry::UnregisterTexture(RenderResourceId id)
	{
		auto it = m_Textures.find(id);
		ASSERT(it != m_Textures.end(), "Texture id not found.");

		// Remove the entire entry (owned+external)
		m_Textures.erase(it);
	}

	void RenderResourceRegistry::UnregisterBuffer(RenderResourceId id)
	{
		auto it = m_Buffers.find(id);
		ASSERT(it != m_Buffers.end(), "Buffer id not found.");

		m_Buffers.erase(it);
	}
} // namespace shz
