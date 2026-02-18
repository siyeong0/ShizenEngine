#include "pch.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

namespace shz
{
    void RenderResourceRegistry::Initialize()
    {
        // Nothing for now.
    }

    void RenderResourceRegistry::Shutdown()
    {
        // Named views first
        for (auto& [id, entry] : m_TextureViews)
        {
            entry.View.Release();
            entry.TextureId = 0;
            entry.ViewType = TEXTURE_VIEW_UNDEFINED;
        }
        m_TextureViews.clear();

        for (auto& [id, entry] : m_BufferViews)
        {
            entry.View.Release();
            entry.BufferId = 0;
            entry.ViewType = BUFFER_VIEW_UNDEFINED;
        }
        m_BufferViews.clear();

        // Owned resources
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

        m_Aliases.clear();
    }

    void RenderResourceRegistry::RegisterTexture(RenderResourceId id, RefCntAutoPtr<ITexture>&& pTexture)
    {
        ASSERT(id != 0, "Id must be non-zero.");
        ASSERT(pTexture, "Cannot register null texture.");
        ASSERT(!HasTexture(id), "Texture id already registered.");

        TextureEntry& e = m_Textures[id];

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
        id = resolveAlias(id);

        // Note: named views referencing this texture are NOT auto-removed (정책 선택).
        // 필요하면 여기서 스캔해서 지우는 코드도 넣을 수 있음.
        m_Textures.erase(id);
    }

    void RenderResourceRegistry::UnregisterBuffer(RenderResourceId id)
    {
        ASSERT(HasBuffer(id), "Buffer id not found.");
        id = resolveAlias(id);
        m_Buffers.erase(id);
    }

    // -----------------------------------------------------------------
    // Default view replace (backward compatible)
    // -----------------------------------------------------------------
    void RenderResourceRegistry::CreateTextureView(RenderResourceId id, const TextureViewDesc& desc)
    {
        ASSERT(id != 0, "Id must be non-zero.");
        ASSERT(HasTexture(id), "Texture id not found.");
        id = resolveAlias(id);

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
        id = resolveAlias(id);

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

    // -----------------------------------------------------------------
    // NEW: Named views (multi-view per texture/buffer)  <<== 핵심
    // -----------------------------------------------------------------
    void RenderResourceRegistry::CreateTextureView(RenderResourceId textureId, RenderResourceId viewId, const TextureViewDesc& desc)
    {
        ASSERT(textureId != 0 && viewId != 0, "Ids must be non-zero.");
        ASSERT(HasTexture(textureId), "Texture id not found.");

        textureId = resolveAlias(textureId);
        ASSERT(m_TextureViews.find(viewId) == m_TextureViews.end(), "Texture view id already exists.");

        TextureEntry& texE = m_Textures[textureId];

        TextureViewEntry ve = {};
        ve.TextureId = textureId;
        ve.ViewType = desc.ViewType;

        texE.Texture->CreateView(desc, &ve.View);
        ASSERT(ve.View, "Failed to create texture view.");

        m_TextureViews.emplace(viewId, std::move(ve));
    }

    void RenderResourceRegistry::CreateBufferView(RenderResourceId bufferId, RenderResourceId viewId, const BufferViewDesc& desc)
    {
        ASSERT(bufferId != 0 && viewId != 0, "Ids must be non-zero.");
        ASSERT(HasBuffer(bufferId), "Buffer id not found.");

        bufferId = resolveAlias(bufferId);
        ASSERT(m_BufferViews.find(viewId) == m_BufferViews.end(), "Buffer view id already exists.");

        BufferEntry& bufE = m_Buffers[bufferId];

        BufferViewEntry ve = {};
        ve.BufferId = bufferId;
        ve.ViewType = desc.ViewType;

        bufE.Buffer->CreateView(desc, &ve.View);
        ASSERT(ve.View, "Failed to create buffer view.");

        m_BufferViews.emplace(viewId, std::move(ve));
    }

    void RenderResourceRegistry::RemoveTextureView(RenderResourceId viewId)
    {
        auto it = m_TextureViews.find(viewId);
        if (it == m_TextureViews.end())
            return;

        it->second.View.Release();
        m_TextureViews.erase(it);
    }

    void RenderResourceRegistry::RemoveBufferView(RenderResourceId viewId)
    {
        auto it = m_BufferViews.find(viewId);
        if (it == m_BufferViews.end())
            return;

        it->second.View.Release();
        m_BufferViews.erase(it);
    }

    // -----------------------------------------------------------------
    // Alias
    // -----------------------------------------------------------------
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

    RenderResourceId RenderResourceRegistry::resolveAlias(RenderResourceId id) const
    {
        auto aliasIter = m_Aliases.find(id);
        if (aliasIter != m_Aliases.end())
            return aliasIter->second;
        return id;
    }

    // -----------------------------------------------------------------
    // Has*
    // -----------------------------------------------------------------
    bool RenderResourceRegistry::HasTexture(RenderResourceId id) const
    {
        id = resolveAlias(id);
        return m_Textures.find(id) != m_Textures.end();
    }

    bool RenderResourceRegistry::HasBuffer(RenderResourceId id) const
    {
        id = resolveAlias(id);
        return m_Buffers.find(id) != m_Buffers.end();
    }

    bool RenderResourceRegistry::HasTextureView(RenderResourceId viewId) const
    {
        return m_TextureViews.find(viewId) != m_TextureViews.end();
    }

    bool RenderResourceRegistry::HasBufferView(RenderResourceId viewId) const
    {
        return m_BufferViews.find(viewId) != m_BufferViews.end();
    }

    // -----------------------------------------------------------------
    // Get resource
    // -----------------------------------------------------------------
    RefCntAutoPtr<ITexture> RenderResourceRegistry::GetTexture(RenderResourceId id) const
    {
        ASSERT(HasTexture(id), "Texture id not found.");
        id = resolveAlias(id);
        return m_Textures.at(id).Texture;
    }

    RefCntAutoPtr<IBuffer> RenderResourceRegistry::GetBuffer(RenderResourceId id) const
    {
        ASSERT(HasBuffer(id), "Buffer id not found.");
        id = resolveAlias(id);
        return m_Buffers.at(id).Buffer;
    }

    // -----------------------------------------------------------------
    // Get default views (by texture/buffer id)
    // -----------------------------------------------------------------
    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureSRV(RenderResourceId id) const
    {
        ASSERT(HasTexture(id), "Texture id not found.");
        id = resolveAlias(id);
        return m_Textures.at(id).SRV;
    }

    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureRTV(RenderResourceId id) const
    {
        ASSERT(HasTexture(id), "Texture id not found.");
        id = resolveAlias(id);
        return m_Textures.at(id).RTV;
    }

    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureDSV(RenderResourceId id) const
    {
        ASSERT(HasTexture(id), "Texture id not found.");
        id = resolveAlias(id);
        return m_Textures.at(id).DSV;
    }

    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureUAV(RenderResourceId id) const
    {
        ASSERT(HasTexture(id), "Texture id not found.");
        id = resolveAlias(id);
        return m_Textures.at(id).UAV;
    }

    RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferSRV(RenderResourceId id) const
    {
        ASSERT(HasBuffer(id), "Buffer id not found.");
        id = resolveAlias(id);
        return m_Buffers.at(id).SRV;
    }

    RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferUAV(RenderResourceId id) const
    {
        ASSERT(HasBuffer(id), "Buffer id not found.");
        id = resolveAlias(id);
        return m_Buffers.at(id).UAV;
    }

    // -----------------------------------------------------------------
    // Get named views (by view id)
    // -----------------------------------------------------------------
    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureView(RenderResourceId viewId) const
    {
        ASSERT(HasTextureView(viewId), "Texture view id not found.");
        return m_TextureViews.at(viewId).View;
    }

    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureSRVView(RenderResourceId viewId) const
    {
        ASSERT(HasTextureView(viewId), "Texture view id not found.");
        const auto& e = m_TextureViews.at(viewId);
        ASSERT(e.ViewType == TEXTURE_VIEW_SHADER_RESOURCE, "Texture view type mismatch.");
        return e.View;
    }

    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureRTVView(RenderResourceId viewId) const
    {
        ASSERT(HasTextureView(viewId), "Texture view id not found.");
        const auto& e = m_TextureViews.at(viewId);
        ASSERT(e.ViewType == TEXTURE_VIEW_RENDER_TARGET, "Texture view type mismatch.");
        return e.View;
    }

    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureDSVView(RenderResourceId viewId) const
    {
        ASSERT(HasTextureView(viewId), "Texture view id not found.");
        const auto& e = m_TextureViews.at(viewId);
        ASSERT(e.ViewType == TEXTURE_VIEW_DEPTH_STENCIL, "Texture view type mismatch.");
        return e.View;
    }

    RefCntAutoPtr<ITextureView> RenderResourceRegistry::GetTextureUAVView(RenderResourceId viewId) const
    {
        ASSERT(HasTextureView(viewId), "Texture view id not found.");
        const auto& e = m_TextureViews.at(viewId);
        ASSERT(e.ViewType == TEXTURE_VIEW_UNORDERED_ACCESS, "Texture view type mismatch.");
        return e.View;
    }

    RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferView(RenderResourceId viewId) const
    {
        ASSERT(HasBufferView(viewId), "Buffer view id not found.");
        return m_BufferViews.at(viewId).View;
    }

    RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferSRVView(RenderResourceId viewId) const
    {
        ASSERT(HasBufferView(viewId), "Buffer view id not found.");
        const auto& e = m_BufferViews.at(viewId);
        ASSERT(e.ViewType == BUFFER_VIEW_SHADER_RESOURCE, "Buffer view type mismatch.");
        return e.View;
    }

    RefCntAutoPtr<IBufferView> RenderResourceRegistry::GetBufferUAVView(RenderResourceId viewId) const
    {
        ASSERT(HasBufferView(viewId), "Buffer view id not found.");
        const auto& e = m_BufferViews.at(viewId);
        ASSERT(e.ViewType == BUFFER_VIEW_UNORDERED_ACCESS, "Buffer view type mismatch.");
        return e.View;
    }

    // -----------------------------------------------------------------
    // Internal entry access (optional; kept minimal)
    // -----------------------------------------------------------------
    const RenderResourceRegistry::TextureEntry* RenderResourceRegistry::getTextureEntryOrNull(RenderResourceId id) const
    {
        id = resolveAlias(id);
        auto it = m_Textures.find(id);
        return it != m_Textures.end() ? &it->second : nullptr;
    }

    RenderResourceRegistry::TextureEntry* RenderResourceRegistry::getTextureEntryOrNull(RenderResourceId id)
    {
        id = resolveAlias(id);
        auto it = m_Textures.find(id);
        return it != m_Textures.end() ? &it->second : nullptr;
    }

    const RenderResourceRegistry::BufferEntry* RenderResourceRegistry::getBufferEntryOrNull(RenderResourceId id) const
    {
        id = resolveAlias(id);
        auto it = m_Buffers.find(id);
        return it != m_Buffers.end() ? &it->second : nullptr;
    }

    RenderResourceRegistry::BufferEntry* RenderResourceRegistry::getBufferEntryOrNull(RenderResourceId id)
    {
        id = resolveAlias(id);
        auto it = m_Buffers.find(id);
        return it != m_Buffers.end() ? &it->second : nullptr;
    }
} // namespace shz
