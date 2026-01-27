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

		TextureLoadInfo tli = {};
		tli.Name = meta.Name.empty() ? "Texture" : meta.Name.c_str();

		tli.IsSRGB = (pSettings != nullptr) ? pSettings->bSRGB : false;
		tli.GenerateMips = (pSettings != nullptr) ? pSettings->bGenerateMips : true;
		tli.FlipVertically = (pSettings != nullptr) ? pSettings->bFlipVertically : false;
		tli.PremultiplyAlpha = (pSettings != nullptr) ? pSettings->bPremultiplyAlpha : false;

		tli.MipFilter = (pSettings != nullptr) ? pSettings->MipFilter : TEXTURE_LOAD_MIP_FILTER_DEFAULT;
		tli.CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;
		tli.Swizzle = (pSettings != nullptr) ? pSettings->Swizzle : TextureComponentMapping::Identity();
		tli.UniformImageClipDim = (pSettings != nullptr) ? pSettings->UniformImageClipDim : 0;

		// ------------------------------------------------------------
		// Format selection
		// ------------------------------------------------------------
		// If you add a field like TextureImportSettings::ForceFormat, use it here.
		// For now: keep previous behavior (RGBA8), with optional SRGB variant.
		{
			const bool bSRGB = tli.IsSRGB;
			tli.Format = bSRGB ? TEX_FORMAT_RGBA8_UNORM_SRGB : TEX_FORMAT_RGBA8_UNORM;
		}

		RefCntAutoPtr<ITextureLoader> pLoader;
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

		// Use the actual output format from loader desc (should match tli.Format)
		const TEXTURE_FORMAT outFmt = desc.Format;

		const uint32 bpp = GetTextureFormatAttribs(outFmt).GetElementSize();
		if (bpp == 0)
		{
			setError(pOutError, "TextureImporter: Unsupported texture format for CPU import.");
			return {};
		}

		Texture tex = {};
		tex.SetFormat(outFmt);

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

			const uint32 dstRowBytes = mipW * bpp;
			tm.Data.resize(static_cast<size_t>(dstRowBytes) * static_cast<size_t>(mipH));

			const uint8* src = reinterpret_cast<const uint8*>(sub.pData);
			const uint64 srcStride = sub.Stride;

			if (srcStride < dstRowBytes)
			{
				ASSERT(false, "TextureImporter: Source stride is smaller than tightly packed row size.");
				setError(pOutError, "TextureImporter: Invalid stride from loader.");
				return {};
			}

			uint8* dst = tm.Data.data();

			for (uint32 y = 0; y < mipH; ++y)
			{
				const uint8* srcRow = src + static_cast<size_t>(y) * static_cast<size_t>(srcStride);
				uint8* dstRow = dst + static_cast<size_t>(y) * static_cast<size_t>(dstRowBytes);
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

