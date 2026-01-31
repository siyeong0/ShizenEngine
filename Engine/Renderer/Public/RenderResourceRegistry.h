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
	using RenderResID = uint64_t;

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
		void RegisterTexture(RenderResID id, RefCntAutoPtr<ITexture>&& pTexure);
		void RegisterBuffer(RenderResID id, RefCntAutoPtr<IBuffer>&& pBuffer);

		// -----------------------------------------------------------------
		// External binding (not owned)
		// Typical use: swapchain backbuffer RTV/DSV, heightmap SRV from scene, etc.
		// - You may bind any subset of views. Unspecified views keep previous values.
		// - Passing all null clears external bindings.
		// -----------------------------------------------------------------
		void BindExternalTextureViews(
			RenderResID id,
			ITexture* pTexure,
			ITextureView* pSRV,
			ITextureView* pRTV,
			ITextureView* pDSV,
			ITextureView* pUAV);

		void BindExternalBuffer(RenderResID id, IBuffer* pBuf);

		// -----------------------------------------------------------------
		// Query
		// - Prefer owned resource if present; otherwise use external.
		// - View getters: for owned textures, returns cached default views if possible.
		// -----------------------------------------------------------------
		ITexture* GetTexture(RenderResID id) const;
		IBuffer* GetBuffer(RenderResID id) const;

		ITextureView* GetSRV(RenderResID id) const;
		ITextureView* GetRTV(RenderResID id) const;
		ITextureView* GetDSV(RenderResID id) const;
		ITextureView* GetUAV(RenderResID id) const;

		// -----------------------------------------------------------------
		// Utilities
		// -----------------------------------------------------------------
		void UnregisterTexture(RenderResID id);
		void UnregisterBuffer(RenderResID id);
		void UnbindExternal(RenderResID id); // clears external texture/buffer bindings (keeps owned)

	private:
		struct TextureEntry final
		{
			RefCntAutoPtr<ITexture> OwnedTexture = {};
			RefCntAutoPtr<ITextureView> OwnedSRV = {};
			RefCntAutoPtr<ITextureView> OwnedRTV = {};
			RefCntAutoPtr<ITextureView> OwnedDSV = {};
			RefCntAutoPtr<ITextureView> OwnedUAV = {};

			ITexture* pExternalTexture = nullptr;
			ITextureView* pExternalSRV = nullptr;
			ITextureView* pExternalRTV = nullptr;
			ITextureView* pExternalDSV = nullptr;
			ITextureView* pExternalUAV = nullptr;

			void ClearOwned()
			{
				OwnedSRV.Release();
				OwnedRTV.Release();
				OwnedDSV.Release();
				OwnedUAV.Release();
				OwnedTexture.Release();
			}

			void ClearExternal()
			{
				pExternalTexture = nullptr;
				pExternalSRV = nullptr;
				pExternalRTV = nullptr;
				pExternalDSV = nullptr;
				pExternalUAV = nullptr;
			}
		};

		struct BufferEntry final
		{
			RefCntAutoPtr<IBuffer> OwnedBuffer = {};
			IBuffer* pExternalBuffer = nullptr;

			void ClearOwned() { OwnedBuffer.Release(); }
			void ClearExternal() { pExternalBuffer = nullptr; }
		};

	private:
		void rebuildOwnedTextureDefaultViews(TextureEntry& e);

	private:
		std::unordered_map<RenderResID, TextureEntry> m_Textures;
		std::unordered_map<RenderResID, BufferEntry>  m_Buffers;
	};
} // namespace shz
