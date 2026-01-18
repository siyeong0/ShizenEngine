#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Public/AssetRef.hpp"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/ISampler.h"

#include "Engine/Material/Public/MaterialTemplate.h"

namespace shz
{
	struct TextureBinding final
	{
		std::string Name = {};

		AssetRef<TextureAsset> TextureRef = {}; // NEW: replaces Handle<TextureAsset>

		ITextureView* pRuntimeView = nullptr;
		ISampler* pSamplerOverride = nullptr;
	};

	class MaterialInstance final
	{
	public:
		MaterialInstance() = default;
		~MaterialInstance() = default;

		MaterialInstance(const MaterialInstance&) = default;
		MaterialInstance& operator=(const MaterialInstance&) = default;

		MaterialInstance(MaterialInstance&&) noexcept = default;
		MaterialInstance& operator=(MaterialInstance&&) noexcept = default;

		bool Initialize(const MaterialTemplate* pTemplate);

		const MaterialTemplate* GetTemplate() const { return m_pTemplate; }

		// ------------------------------------------------------------
		// Values
		// ------------------------------------------------------------
		bool SetFloat(const char* name, float  v);
		bool SetFloat2(const char* name, const float v[2]);
		bool SetFloat3(const char* name, const float v[3]);
		bool SetFloat4(const char* name, const float v[4]);

		bool SetInt(const char* name, int32  v);
		bool SetInt2(const char* name, const int32 v[2]);
		bool SetInt3(const char* name, const int32 v[3]);
		bool SetInt4(const char* name, const int32 v[4]);

		bool SetUint(const char* name, uint32  v);
		bool SetUint2(const char* name, const uint32 v[2]);
		bool SetUint3(const char* name, const uint32 v[3]);
		bool SetUint4(const char* name, const uint32 v[4]);

		bool SetFloat4x4(const char* name, const float m16[16]);

		bool SetRaw(const char* name, const void* pData, uint32 byteSize);

		// ------------------------------------------------------------
		// Resources
		// ------------------------------------------------------------

		// NEW: Set by AssetRef (new AssetManagerImpl path)
		bool SetTextureAssetRef(const char* textureName, const AssetRef<TextureAsset>& textureRef);

		// Runtime SRV override (editor/debug)
		bool SetTextureRuntimeView(const char* textureName, ITextureView* pView);

		bool SetSamplerOverride(const char* textureName, ISampler* pSampler);

		// ------------------------------------------------------------
		// For RenderResourceCache
		// ------------------------------------------------------------
		uint32 GetCBufferBlobCount() const { return static_cast<uint32>(m_CBufferBlobs.size()); }
		const uint8* GetCBufferBlobData(uint32 cbufferIndex) const;
		uint32 GetCBufferBlobSize(uint32 cbufferIndex) const;

		bool IsCBufferDirty(uint32 cbufferIndex) const;
		void ClearCBufferDirty(uint32 cbufferIndex);

		uint32 GetTextureBindingCount() const { return static_cast<uint32>(m_TextureBindings.size()); }
		const TextureBinding& GetTextureBinding(uint32 index) const { return m_TextureBindings[index]; }

		bool IsTextureDirty(uint32 resourceIndex) const;
		void ClearTextureDirty(uint32 resourceIndex);

		void MarkAllDirty();

	private:
		bool writeValueInternal(const char* name, const void* pData, uint32 byteSize, MATERIAL_VALUE_TYPE expectedValueType);

		static inline bool isTextureType(MATERIAL_RESOURCE_TYPE t)
		{
			return (t == MATERIAL_RESOURCE_TYPE_TEXTURE2D) ||
				(t == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ||
				(t == MATERIAL_RESOURCE_TYPE_TEXTURECUBE);
		}

	private:
		const MaterialTemplate* m_pTemplate = nullptr;

		std::vector<std::vector<uint8>> m_CBufferBlobs = {};
		std::vector<uint8> m_bCBufferDirties = {};

		std::vector<TextureBinding> m_TextureBindings = {};
		std::vector<uint8> m_bTextureDirties = {};
	};

} // namespace shz
