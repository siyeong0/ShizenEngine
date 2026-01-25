#include "pch.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"

#include <algorithm>

#include "Tools/Image/Public/TextureLoader.h"

namespace shz
{
	static inline uint32 clampMipDim(uint32 v) noexcept
	{
		if (v == 0)
		{
			return 0;
		}
		return (v > 0) ? v : 1;
	}

	static inline void setError(std::string* pOutError, const char* msg)
	{
		if (pOutError)
		{
			*pOutError = (msg != nullptr) ? msg : "Unknown error.";
		}
	}

	static inline void setError(std::string* pOutError, const std::string& msg)
	{
		if (pOutError)
		{
			*pOutError = msg;
		}
	}

	std::unique_ptr<AssetObject> TextureImporter::operator()(
		AssetManager& /*assetManager*/,
		const AssetMeta& meta,
		uint64* pOutResidentBytes,
		std::string* pOutError) const
	{
		ASSERT(pOutResidentBytes != nullptr, "Invalid argument. pOutResidentBytes is null.");
		*pOutResidentBytes = 0;

		// ------------------------------------------------------------
		// Validate meta
		// ------------------------------------------------------------
		if (meta.SourcePath.empty())
		{
			ASSERT(false, "TextureImporter: meta.SourcePath is empty.");
			setError(pOutError, "TextureImporter: SourcePath is empty.");
			return {};
		}

		// Import settings are stored in meta payload (import/export only).
		const TextureImportSettings* pSettings = meta.TryGetTextureMeta();
		if (!pSettings)
		{
			// It's valid to have no payload, but then we use defaults.
		}

		// ------------------------------------------------------------
		// Build TextureLoadInfo (force RGBA8 in system memory)
		// ------------------------------------------------------------
		TextureLoadInfo tli = {};
		tli.Name = meta.Name.empty() ? "TextureAsset" : meta.Name.c_str();

		// We are not creating GPU texture here, but TextureLoader still needs fields.
		// Keep defaults.
		tli.IsSRGB = (pSettings != nullptr) ? pSettings->bSRGB : false;
		tli.GenerateMips = (pSettings != nullptr) ? pSettings->bGenerateMips : true;
		tli.FlipVertically = (pSettings != nullptr) ? pSettings->bFlipVertically : false;
		tli.PremultiplyAlpha = (pSettings != nullptr) ? pSettings->bPremultiplyAlpha : false;

		tli.MipFilter = (pSettings != nullptr) ? pSettings->MipFilter : TEXTURE_LOAD_MIP_FILTER_DEFAULT;
		tli.CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		tli.Swizzle = (pSettings != nullptr) ? pSettings->Swizzle : TextureComponentMapping::Identity();
		tli.UniformImageClipDim = (pSettings != nullptr) ? pSettings->UniformImageClipDim : 0;

		// IMPORTANT:
		// We want system-memory RGBA8. Force loader output format to RGBA8.
		tli.Format = TEX_FORMAT_RGBA8_UNORM;

		RefCntAutoPtr<ITextureLoader> pLoader;

		// Let loader detect the file format from content.
		CreateTextureLoaderFromFile(meta.SourcePath.c_str(), IMAGE_FILE_FORMAT_UNKNOWN, tli, &pLoader);

		if (!pLoader)
		{
			setError(pOutError, "TextureImporter: Failed to create texture loader.");
			return {};
		}

		const TextureDesc& desc = pLoader->GetTextureDesc();

		if (desc.Width == 0 || desc.Height == 0 || desc.MipLevels == 0)
		{
			ASSERT(false, "TextureImporter: Invalid texture desc from loader.");
			setError(pOutError, "TextureImporter: Invalid texture data.");
			return {};
		}

		// ------------------------------------------------------------
		// Fill TextureAsset mips (RGBA8 tightly packed)
		// ------------------------------------------------------------
		// NOTE:
		// This assumes TextureAsset is (or can be) returned as AssetObject.
		// If TextureAsset does not inherit AssetObject, wrap it in an AssetObject.
		Texture tex = {};

		std::vector<TextureMip>& mips = tex.GetMips();
		mips.clear();
		mips.reserve(desc.MipLevels);

		uint64 totalBytes = 0;

		for (uint32 mip = 0; mip < desc.MipLevels; ++mip)
		{
			const TextureSubResData& sub = pLoader->GetSubresourceData(mip, 0);

			const uint32 mipW = std::max(1u, desc.Width >> mip);
			const uint32 mipH = std::max(1u, desc.Height >> mip);

			if (sub.pData == nullptr)
			{
				ASSERT(false, "TextureImporter: Subresource data is null.");
				setError(pOutError, "TextureImporter: Subresource data is null.");
				return {};
			}

			TextureMip tm = {};
			tm.Width = mipW;
			tm.Height = mipH;

			const uint32 dstRowBytes = mipW * 4u; // RGBA8
			tm.RGBA.resize(static_cast<size_t>(dstRowBytes) * static_cast<size_t>(mipH));

			// TextureLoader may return padded stride. Copy row-by-row safely.
			const uint8* src = reinterpret_cast<const uint8*>(sub.pData);
			const uint64 srcStride = sub.Stride;

			ASSERT(srcStride >= dstRowBytes, "TextureImporter: Source stride is smaller than tightly packed row size.");

			uint8* dst = tm.RGBA.data();

			for (uint32 y = 0; y < mipH; ++y)
			{
				const uint8* srcRow = src + static_cast<size_t>(y) * static_cast<size_t>(srcStride);
				uint8* dstRow = dst + static_cast<size_t>(y) * static_cast<size_t>(dstRowBytes);
				std::memcpy(dstRow, srcRow, dstRowBytes);
			}

			totalBytes += static_cast<uint64>(tm.RGBA.size());

			mips.emplace_back(static_cast<TextureMip&&>(tm));
		}

		// Validate the asset we produced.
		if (!tex.IsValid())
		{
			ASSERT(false, "TextureImporter: Produced TextureAsset is invalid.");
			setError(pOutError, "TextureImporter: Produced TextureAsset is invalid.");
			return {};
		}

		*pOutResidentBytes = totalBytes;

		// ------------------------------------------------------------
		// Return as AssetObject
		// ------------------------------------------------------------
		return std::make_unique<TypedAssetObject<Texture>>(tex);
	}

} // namespace shz
