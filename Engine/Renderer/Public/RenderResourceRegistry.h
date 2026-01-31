#pragma once
#include <cstdint>
#include <unordered_map>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IBuffer.h"

namespace shz
{
	// NOTE:
	// - This registry is a lightweight lookup table for shared render resources.
	// - It does NOT create, resize, or own "scene asset" textures/buffers.
	// - It only stores pointers (owned via RefCntAutoPtr or external raw pointers).
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
		// - If the same id already exists, it will be overwritten.
		// - Passing null removes the owned entry (but external binding may remain).
		// -----------------------------------------------------------------
		void RegisterTexture(RenderResourceId id, RefCntAutoPtr<ITexture>&& pTexure);
		void RegisterBuffer(RenderResourceId id, RefCntAutoPtr<IBuffer>&& pBuffer);

		void CreateTextureView(RenderResourceId id, const TextureViewDesc& desc);
		void CreateBufferView(RenderResourceId id, const BufferViewDesc& desc);

		// -----------------------------------------------------------------
		// Query
		// - Prefer owned resource if present; otherwise use external.
		// - View getters: for owned textures, returns cached default views if possible.
		// -----------------------------------------------------------------
		ITexture* GetTexture(RenderResourceId id) const;
		IBuffer* GetBuffer(RenderResourceId id) const;

		ITextureView* GetTextureSRV(RenderResourceId id) const;
		ITextureView* GetTextureRTV(RenderResourceId id) const;
		ITextureView* GetTextureDSV(RenderResourceId id) const;
		ITextureView* GetTextureUAV(RenderResourceId id) const;

		IBufferView* GetBufferSRV(RenderResourceId id) const;
		IBufferView* GetBufferUAV(RenderResourceId id) const;

		// -----------------------------------------------------------------
		// Utilities
		// -----------------------------------------------------------------
		void UnregisterTexture(RenderResourceId id);
		void UnregisterBuffer(RenderResourceId id);

	private:
		struct TextureEntry final
		{
			RefCntAutoPtr<ITexture> Texture = {};
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

	private:
		std::unordered_map<RenderResourceId, TextureEntry> m_Textures;
		std::unordered_map<RenderResourceId, BufferEntry>  m_Buffers;
	};
} // namespace shz
