#include "pch.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"

#include <algorithm>

#include "Engine/Image/Public/TextureLoader.h"

namespace shz
{
	static inline void setError(std::string* pOutError, const char* msg)
	{
		if (pOutError) *pOutError = (msg != nullptr) ? msg : "Unknown error.";
	}
	static inline void setError(std::string* pOutError, const std::string& msg)
	{
		if (pOutError) *pOutError = msg;
	}

	std::unique_ptr<AssetObject> TextureImporter::operator()(
		AssetManager& /*assetManager*/,
		const AssetMeta& meta,
		uint64* pOutResidentBytes,
		std::string* pOutError) const
	{
		ASSERT(pOutResidentBytes != nullptr, "Invalid argument. pOutResidentBytes is null.");
		*pOutResidentBytes = 0;

		if (meta.SourcePath.empty())
		{
			ASSERT(false, "TextureImporter: meta.SourcePath is empty.");
			setError(pOutError, "TextureImporter: SourcePath is empty.");
			return {};
		}

		const TextureImportSettings* pSettings = meta.TryGetTextureMeta();

		// ------------------------------------------------------------
		// Build loader request
		// ------------------------------------------------------------
		// Key idea:
		// - Prefer the loader's "natural" output format (TEX_FORMAT_UNKNOWN),
		//   unless you explicitly need to force a format.
		// - SRGB is an intent. If you force SRGB for data textures (heightmaps, masks),
		//   you'll corrupt the values. So only apply SRGB when requested.
		TextureLoadInfo tli = {};
		tli.Name = meta.Name.empty() ? "Texture" : meta.Name.c_str();

		tli.IsSRGB = (pSettings != nullptr) ? pSettings->bSRGB : false;

		// NOTE:
		// - For CPU textures, storing mips can be huge. But if user requests mips,
		//   keep them (desc.MipLevels will reflect that).
		// - For heightmaps and other data textures, you usually want GenerateMips=false.
		tli.GenerateMips = (pSettings != nullptr) ? pSettings->bGenerateMips : true;

		tli.FlipVertically = (pSettings != nullptr) ? pSettings->bFlipVertically : false;
		tli.PremultiplyAlpha = (pSettings != nullptr) ? pSettings->bPremultiplyAlpha : false;

		tli.MipFilter = (pSettings != nullptr) ? pSettings->MipFilter : TEXTURE_LOAD_MIP_FILTER_DEFAULT;

		// Always disable compression for CPU container (we store raw bytes per pixel).
		tli.CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		tli.Swizzle = (pSettings != nullptr) ? pSettings->Swizzle : TextureComponentMapping::Identity();

		// If this is non-zero, you are explicitly asking the loader to clip/resize uniformly.
		// That WILL change Width/Height => affects terrain mesh density if used for heightmaps.
		tli.UniformImageClipDim = (pSettings != nullptr) ? pSettings->UniformImageClipDim : 0;

		// Let the loader decide output format by default.
		// If you later want a "ForceFormat" setting, apply it here.
		tli.Format = TEX_FORMAT_UNKNOWN;

		RefCntAutoPtr<ITextureLoader> pLoader;
		CreateTextureLoaderFromFile(meta.SourcePath.c_str(), IMAGE_FILE_FORMAT_UNKNOWN, tli, &pLoader);

		if (!pLoader)
		{
			setError(pOutError, "TextureImporter: Failed to create texture loader.");
			return {};
		}

		// ------------------------------------------------------------
		// Validate loader output descriptor
		// ------------------------------------------------------------
		const TextureDesc& desc = pLoader->GetTextureDesc();

		// This importer currently supports only 2D, single-slice CPU textures.
		// (Extend as needed: arrays, cubemaps, volume textures.)
		if (desc.Width == 0 || desc.Height == 0 || desc.MipLevels == 0)
		{
			ASSERT(false, "TextureImporter: Invalid texture desc from loader.");
			setError(pOutError, "TextureImporter: Invalid texture desc from loader.");
			return {};
		}

		if (desc.Type != RESOURCE_DIM_TEX_2D)
		{
			setError(pOutError, "TextureImporter: Only 2D textures are supported for CPU import.");
			return {};
		}

		if (desc.ArraySize != 1)
		{
			setError(pOutError, "TextureImporter: Texture arrays are not supported in this CPU importer (ArraySize must be 1).");
			return {};
		}

		const TEXTURE_FORMAT outFmt = desc.Format;
		if (outFmt == TEX_FORMAT_UNKNOWN)
		{
			setError(pOutError, "TextureImporter: Loader returned TEX_FORMAT_UNKNOWN.");
			return {};
		}

		const TextureFormatAttribs& fmtAttribs = GetTextureFormatAttribs(outFmt);

		// Our CPU container assumes "uncompressed, tightly-packable" rows:
		// rowBytes = width * elementSize.
		// For block-compressed formats, this assumption breaks.
		//
		// Diligent's GetElementSize() is typically 0 for block-compressed formats.
		const uint32 bpp = fmtAttribs.GetElementSize();
		if (bpp == 0)
		{
			setError(pOutError, "TextureImporter: Unsupported or compressed texture format for CPU import (element size is 0).");
			return {};
		}

		// ------------------------------------------------------------
		// Allocate CPU texture container
		// ------------------------------------------------------------
		Texture tex = {};
		tex.SetFormat(outFmt);

		std::vector<TextureMip>& mips = tex.GetMips();
		mips.clear();
		mips.reserve(desc.MipLevels);

		uint64 totalBytes = 0;

		// ------------------------------------------------------------
		// Copy each mip row-by-row (strip loader stride to tight rows)
		// ------------------------------------------------------------
		for (uint32 mip = 0; mip < desc.MipLevels; ++mip)
		{
			const uint32 mipW = std::max(1u, desc.Width >> mip);
			const uint32 mipH = std::max(1u, desc.Height >> mip);

			const TextureSubResData& sub = pLoader->GetSubresourceData(mip, /*ArraySlice*/0);

			if (sub.pData == nullptr)
			{
				ASSERT(false, "TextureImporter: Subresource data is null.");
				setError(pOutError, "TextureImporter: Subresource data is null.");
				return {};
			}

			// Tightly packed row bytes for this mip.
			const uint32 dstRowBytes = mipW * bpp;

			// Loader stride is in bytes per row for this subresource.
			const uint64 srcStride = sub.Stride;

			// Basic sanity.
			if (srcStride < static_cast<uint64>(dstRowBytes))
			{
				ASSERT(false, "TextureImporter: Source stride smaller than tight row bytes.");
				setError(pOutError, "TextureImporter: Invalid stride from loader.");
				return {};
			}

			TextureMip tm = {};
			tm.Width = mipW;
			tm.Height = mipH;
			tm.Data.resize(static_cast<size_t>(dstRowBytes) * static_cast<size_t>(mipH));

			const uint8* srcBase = reinterpret_cast<const uint8*>(sub.pData);
			uint8* dstBase = tm.Data.data();

			for (uint32 y = 0; y < mipH; ++y)
			{
				const uint8* srcRow = srcBase + static_cast<size_t>(y) * static_cast<size_t>(srcStride);
				uint8* dstRow = dstBase + static_cast<size_t>(y) * static_cast<size_t>(dstRowBytes);
				std::memcpy(dstRow, srcRow, dstRowBytes);
			}

			totalBytes += static_cast<uint64>(tm.Data.size());
			mips.emplace_back(static_cast<TextureMip&&>(tm));
		}

		if (!tex.IsValid())
		{
			ASSERT(false, "TextureImporter: Produced Texture is invalid.");
			setError(pOutError, "TextureImporter: Produced Texture is invalid.");
			return {};
		}

		*pOutResidentBytes = totalBytes;
		return std::make_unique<TypedAssetObject<Texture>>(tex);
	}

}

