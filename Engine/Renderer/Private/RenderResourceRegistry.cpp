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
		ASSERT(!HasTexture(id), "Texture id already registered."); // TODO: Duplicate

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

	void RenderResourceRegistry::AddAlias(uint64 src, uint64 alias)
	{
		ASSERT(m_Aliases.find(alias) == m_Aliases.end(), "Alias already exists.");
		ASSERT(m_Textures.find(src) != m_Textures.end() || m_Buffers.find(src) != m_Buffers.end(), "Source resource not exists.");

		m_Aliases[alias] = src;
	}

	void RenderResourceRegistry::RemoveAlias(uint64 alias)
	{
		ASSERT(m_Aliases.find(alias) != m_Aliases.end(), "Alias not exists.");

		m_Aliases.erase(alias);
	}

	bool RenderResourceRegistry::HasTexture(RenderResourceId id) const
	{
		auto it = m_Textures.find(id);
		if (it != m_Textures.end())
		{
			return true;
		}

		auto aliasIter = m_Aliases.find(id);
		if (aliasIter != m_Aliases.end())
		{
			ASSERT(m_Textures.find(aliasIter->second) != m_Textures.end(), "Invalid alias.");
			return true;
		}

		return false;
	}

	bool RenderResourceRegistry::HasBuffer(RenderResourceId id) const
	{
		auto it = m_Buffers.find(id);
		if (it != m_Buffers.end())
		{
			return true;
		}

		auto aliasIter = m_Aliases.find(id);
		if (aliasIter != m_Aliases.end())
		{
			ASSERT(m_Buffers.find(aliasIter->second) != m_Buffers.end(), "Invalid alias.");
			return true;
		}

		return false;
	}

	RefCntAutoPtr<ITexture> RenderResourceRegistry::GetTexture(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");

		return getTextureEntryOrNull(id)->Texture;
	}

	RefCntAutoPtr<IBuffer> RenderResourceRegistry::GetBuffer(RenderResourceId id) const
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");

		return getBufferEntryOrNull(id)->Buffer;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureSRV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = *getTextureEntryOrNull(id);
		return e.SRV;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureRTV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = *getTextureEntryOrNull(id);
		return e.RTV;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureDSV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = *getTextureEntryOrNull(id);
		return e.DSV;
	}

	RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureUAV(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");
		const TextureEntry& e = *getTextureEntryOrNull(id);
		return e.UAV;
	}

	RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferSRV(RenderResourceId id) const
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");
		const BufferEntry& e = *getBufferEntryOrNull(id);
		return e.SRV;
	}

	RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferUAV(RenderResourceId id) const
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");
		const BufferEntry& e = *getBufferEntryOrNull(id);
		return e.UAV;
	}

	const RenderResourceRegistry::TextureEntry* RenderResourceRegistry::getTextureEntryOrNull(RenderResourceId id) const
	{
		ASSERT(HasTexture(id), "Texture id not found.");

		auto it = m_Textures.find(id);
		if (it != m_Textures.end())
		{
			return &m_Textures.at(id);
		}

		auto aliasIter = m_Aliases.find(id);
		if (aliasIter != m_Aliases.end())
		{
			ASSERT(m_Textures.find(aliasIter->second) != m_Textures.end(), "Invalid alias.");
			return &m_Textures.at(aliasIter->second);
		}

		ASSERT(false, "Invalid id");
		return nullptr;
	}

	const RenderResourceRegistry::BufferEntry* RenderResourceRegistry::getBufferEntryOrNull(RenderResourceId id) const
	{
		ASSERT(HasBuffer(id), "Buffer id not found.");

		auto it = m_Buffers.find(id);
		if (it != m_Buffers.end())
		{
			return &m_Buffers.at(id);
		}

		auto aliasIter = m_Aliases.find(id);
		if (aliasIter != m_Aliases.end())
		{
			ASSERT(m_Buffers.find(aliasIter->second) != m_Buffers.end(), "Invalid alias.");
			return &m_Buffers.at(aliasIter->second);
		}

		ASSERT(false, "Invalid id");

		return nullptr;
	}
} // namespace shz
