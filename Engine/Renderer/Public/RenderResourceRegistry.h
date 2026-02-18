#pragma once
#include <cstdint>
#include <unordered_map>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IBufferView.h"

namespace shz
{
    using RenderResourceId = uint64_t;

    class RenderResourceRegistry final
    {
    public:
        RenderResourceRegistry() = default;
        ~RenderResourceRegistry() = default;

        RenderResourceRegistry(const RenderResourceRegistry&) = delete;
        RenderResourceRegistry& operator=(const RenderResourceRegistry&) = delete;

        void Initialize();
        void Shutdown();

        // -----------------------------------------------------------------
        // Owned registration (registry holds RefCnt references)
        // -----------------------------------------------------------------
        void RegisterTexture(RenderResourceId id, RefCntAutoPtr<ITexture>&& pTexure);
        void RegisterBuffer(RenderResourceId id, RefCntAutoPtr<IBuffer>&& pBuffer);

        void UnregisterTexture(RenderResourceId id);
        void UnregisterBuffer(RenderResourceId id);

        // -----------------------------------------------------------------
        // View creation
        // 1) "Default view replace" (backward compatible): view stored inside TextureEntry/BufferEntry
        // 2) "Named view": view stored in separate map (supports per-slice views for arrays)
        // -----------------------------------------------------------------
        void CreateTextureView(RenderResourceId textureId, const TextureViewDesc& desc); // replaces cached default view (SRV/RTV/DSV/UAV)
        void CreateBufferView(RenderResourceId bufferId, const BufferViewDesc& desc);   // replaces cached default view (SRV/UAV)

        void CreateTextureView(RenderResourceId textureId, RenderResourceId viewId, const TextureViewDesc& desc);
        void CreateBufferView(RenderResourceId bufferId, RenderResourceId viewId, const BufferViewDesc& desc); 

        void RemoveTextureView(RenderResourceId viewId);
        void RemoveBufferView(RenderResourceId viewId);

        void AddAlias(uint64 src, uint64 alias);
        void RemoveAlias(uint64 alias);

        // -----------------------------------------------------------------
        // Query
        // -----------------------------------------------------------------
        bool HasTexture(RenderResourceId id) const;
        bool HasBuffer(RenderResourceId id) const;

        // "Named view" query
        bool HasTextureView(RenderResourceId viewId) const;
        bool HasBufferView(RenderResourceId viewId) const;

        RefCntAutoPtr<ITexture> GetTexture(RenderResourceId id) const;
        RefCntAutoPtr<IBuffer> GetBuffer(RenderResourceId id) const;

        // Default views (by texture/buffer id)
        RefCntAutoPtr<ITextureView> GetTextureSRV(RenderResourceId textureId) const;
        RefCntAutoPtr<ITextureView> GetTextureRTV(RenderResourceId textureId) const;
        RefCntAutoPtr<ITextureView> GetTextureDSV(RenderResourceId textureId) const;
        RefCntAutoPtr<ITextureView> GetTextureUAV(RenderResourceId textureId) const;

        RefCntAutoPtr<IBufferView> GetBufferSRV(RenderResourceId bufferId) const;
        RefCntAutoPtr<IBufferView> GetBufferUAV(RenderResourceId bufferId) const;

        // Named views (by view id)  <<-- cascade slice DSV는 여기로 가져오게 됨
        RefCntAutoPtr<ITextureView> GetTextureView(RenderResourceId viewId) const;
        RefCntAutoPtr<ITextureView> GetTextureSRVView(RenderResourceId viewId) const;
        RefCntAutoPtr<ITextureView> GetTextureRTVView(RenderResourceId viewId) const;
        RefCntAutoPtr<ITextureView> GetTextureDSVView(RenderResourceId viewId) const;
        RefCntAutoPtr<ITextureView> GetTextureUAVView(RenderResourceId viewId) const;

        RefCntAutoPtr<IBufferView> GetBufferView(RenderResourceId viewId) const;
        RefCntAutoPtr<IBufferView> GetBufferSRVView(RenderResourceId viewId) const;
        RefCntAutoPtr<IBufferView> GetBufferUAVView(RenderResourceId viewId) const;

    private:
        struct TextureEntry final
        {
            RefCntAutoPtr<ITexture> Texture = {};
            // Cached default views (one per type)
            RefCntAutoPtr<ITextureView> SRV = {};
            RefCntAutoPtr<ITextureView> RTV = {};
            RefCntAutoPtr<ITextureView> DSV = {};
            RefCntAutoPtr<ITextureView> UAV = {};
        };

        struct BufferEntry final
        {
            RefCntAutoPtr<IBuffer> Buffer = {};
            RefCntAutoPtr<IBufferView> SRV = {};
            RefCntAutoPtr<IBufferView> UAV = {};
        };

        struct TextureViewEntry final
        {
            RenderResourceId TextureId = 0; // original texture id (resolved)
            TEXTURE_VIEW_TYPE ViewType = TEXTURE_VIEW_UNDEFINED;
            RefCntAutoPtr<ITextureView> View = {};
        };

        struct BufferViewEntry final
        {
            RenderResourceId BufferId = 0; // original buffer id (resolved)
            BUFFER_VIEW_TYPE ViewType = BUFFER_VIEW_UNDEFINED;
            RefCntAutoPtr<IBufferView> View = {};
        };

        const TextureEntry* getTextureEntryOrNull(RenderResourceId id) const;
        TextureEntry* getTextureEntryOrNull(RenderResourceId id);

        const BufferEntry* getBufferEntryOrNull(RenderResourceId id) const;
        BufferEntry* getBufferEntryOrNull(RenderResourceId id);

        RenderResourceId resolveAlias(RenderResourceId id) const;

    private:
        std::unordered_map<RenderResourceId, TextureEntry> m_Textures;
        std::unordered_map<RenderResourceId, BufferEntry> m_Buffers;

        std::unordered_map<RenderResourceId, TextureViewEntry> m_TextureViews;
        std::unordered_map<RenderResourceId, BufferViewEntry>  m_BufferViews;

        std::unordered_map<RenderResourceId, RenderResourceId> m_Aliases;
    };
} // namespace shz
