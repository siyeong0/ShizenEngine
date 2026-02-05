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
		for (auto& [id, entry] : m_Textures)
		{
			entry.SRV.Release();
			entry.RTV.Release();
			entry.DSV.Release();
			entry.UAV.Release();
			entry.Texture.Release();
		}
		m_Textures.clear();

		for (auto& [id, entry] : m_Buffers)
		{
			entry.SRV.Release();
			entry.UAV.Release();
			entry.Buffer.Release();
		}
		m_Buffers.clear();
	}

	void RenderResourceRegistry::RegisterTexture(RenderResourceId id, RefCntAutoPtr<ITexture>&& pTexture)
	{
		ASSERT(id != 0, "Id must be non-zero.");
		ASSERT(pTexture, "Cannot register null texture.");
		// ASSERT(!HasTexture(id), "Texture id already registered."); // TODO: Duplicate

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
		ASSERT(pBuffer, "Cannot register null buffer.");
		ASSERT(!HasBuffer(id), "Buffer id already registered.");

		BufferEntry& e = m_Buffers[id];
		e.Buffer = std::move(pBuffer);
		e.SRV = e.Buffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
		e.UAV = e.Buffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS);
	}

	void RenderResourceRegistry::UnregisterTexture(RenderResourceId id)
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		m_Textures.erase(id);
	}

	void RenderResourceRegistry::UnregisterBuffer(RenderResourceId id)
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");
		m_Buffers.erase(id);
	}

	void RenderResourceRegistry::CreateTextureView(RenderResourceId id, const TextureViewDesc& desc)
	{
		ASSERT(id != 0, "Id must be non-zero.");
		ASSERT(HasTexture(id), "Texture id not found.");

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
		ASSERT(HasBuffer(id), "Buffer id not found.");

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

	bool RenderResourceRegistry::HasTexture(RenderResourceId id) const
	{
		auto it = m_Textures.find(id);
		return it != m_Textures.end();
	}

	bool RenderResourceRegistry::HasBuffer(RenderResourceId id) const
	{
		auto it = m_Buffers.find(id);
		return it != m_Buffers.end();
	}

	RefCntAutoPtr<ITexture> RenderResourceRegistry::GetTexture(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = m_Textures.at(id);
		return e.Texture;
	}

	RefCntAutoPtr<IBuffer> RenderResourceRegistry::GetBuffer(RenderResourceId id) const
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");
		const BufferEntry& e = m_Buffers.at(id);
		return e.Buffer;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureSRV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = m_Textures.at(id);
		return e.SRV;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureRTV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = m_Textures.at(id);
		return e.RTV;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureDSV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = m_Textures.at(id);
		return e.DSV;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureUAV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = m_Textures.at(id);
		return e.UAV;
	}

	RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferSRV(RenderResourceId id) const
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");
		const BufferEntry& e = m_Buffers.at(id);
		return e.SRV;
	}

	RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferUAV(RenderResourceId id) const
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");
		const BufferEntry& e = m_Buffers.at(id);
		return e.UAV;
	}
} // namespace shz
