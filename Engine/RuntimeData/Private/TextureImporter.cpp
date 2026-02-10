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
		const TextureImportSettings& setting = (pSettings != nullptr) ? *pSettings : TextureImportSettings{};
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

		tli.IsSRGB = setting.bSRGB;

		// NOTE:
		// - For CPU textures, storing mips can be huge. But if user requests mips,
		//   keep them (desc.MipLevels will reflect that).
		// - For heightmaps and other data textures, you usually want GenerateMips=false.
		tli.GenerateMips = setting.bGenerateMips;

		tli.FlipVertically = setting.bFlipVertically;
		tli.PremultiplyAlpha = setting.bPremultiplyAlpha;
		tli.MipFilter = setting.MipFilter;

		// Always disable compression for CPU container (we store raw bytes per pixel).
		tli.CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		tli.Swizzle = setting.Swizzle;

		// If this is non-zero, you are explicitly asking the loader to clip/resize uniformly.
		// That WILL change Width/Height => affects terrain mesh density if used for heightmaps.
		tli.UniformImageClipDim = setting.UniformImageClipDim;
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

		// ------------------------------------------------------------
// Preserve alpha test coverage across mip chain (optional)
// ------------------------------------------------------------
// Fix: binary alpha (0/1) gets averaged in lower mips, causing masked foliage/grass
//      to shrink/disappear at distance.
//
// Strategy:
//  - Compute target coverage from mip0: fraction(alpha > cutoff)
//  - For each mip>0, find scale s (binary search) such that
//      fraction(saturate(alpha*s) > cutoff) ~= target
//  - Apply alpha = saturate(alpha*s)
//
// Safety:
//  - Auto-skip if texture is effectively opaque (avoid affecting opaque/basecolor textures).
//  - Only supports 8-bit RGBA/BGRA formats here.
		if (setting.bPreserveAlphaCoverage && desc.MipLevels > 1)
		{
			const float alphaCutoff01 = Clamp(setting.AlphaCutoff, 0.0f, 1.0f);

			bool supportedFmt = false;
			bool isBGRA = false;

			switch (outFmt)
			{
			case TEX_FORMAT_RGBA8_UNORM:
			case TEX_FORMAT_RGBA8_UNORM_SRGB:
				supportedFmt = true;
				isBGRA = false;
				break;

			case TEX_FORMAT_BGRA8_UNORM:
			case TEX_FORMAT_BGRA8_UNORM_SRGB:
				supportedFmt = true;
				isBGRA = true;
				break;

			default:
				supportedFmt = false;
				break;
			}

			if (supportedFmt)
			{
				// For RGBA8/BGRA8, alpha is at byte 3.
				const uint32 alphaByteOffset = 3u;
				const uint32 bytesPerPixel = bpp; // expected 4


				// Local helpers (lambdas)
				auto ComputeCoverageU8 = [&](const TextureMip& mip, uint8 cutoffU8) -> float
					{
						const uint32 w = mip.Width;
						const uint32 h = mip.Height;
						const uint8* data = mip.Data.data();

						uint64 pass = 0;
						const uint64 count = static_cast<uint64>(w) * static_cast<uint64>(h);
						const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

						for (uint32 y = 0; y < h; ++y)
						{
							const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
							for (uint32 x = 0; x < w; ++x)
							{
								const uint8 a = row[static_cast<size_t>(x) * bytesPerPixel + alphaByteOffset];
								pass += (a > cutoffU8) ? 1u : 0u;
							}
						}

						return (count > 0) ? (static_cast<float>(pass) / static_cast<float>(count)) : 0.0f;
					};

				auto CoverageWithScale = [&](const TextureMip& mip, float scale, float cutoff01f) -> float
					{
						const uint32 w = mip.Width;
						const uint32 h = mip.Height;
						const uint8* data = mip.Data.data();

						uint64 pass = 0;
						const uint64 count = static_cast<uint64>(w) * static_cast<uint64>(h);
						const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

						for (uint32 y = 0; y < h; ++y)
						{
							const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
							for (uint32 x = 0; x < w; ++x)
							{
								const uint8 aU8 = row[static_cast<size_t>(x) * bytesPerPixel + alphaByteOffset];
								float a = (static_cast<float>(aU8) / 255.0f) * scale;
								a = std::min(std::max(a, 0.0f), 1.0f);
								pass += (a > cutoff01f) ? 1u : 0u;
							}
						}

						return (count > 0) ? (static_cast<float>(pass) / static_cast<float>(count)) : 0.0f;
					};

				auto ApplyScale = [&](TextureMip& mip, float scale)
					{
						const uint32 w = mip.Width;
						const uint32 h = mip.Height;
						uint8* data = mip.Data.data();

						const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

						for (uint32 y = 0; y < h; ++y)
						{
							uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
							for (uint32 x = 0; x < w; ++x)
							{
								uint8& aU8 = row[static_cast<size_t>(x) * bytesPerPixel + alphaByteOffset];
								float a = (static_cast<float>(aU8) / 255.0f) * scale;
								a = std::min(std::max(a, 0.0f), 1.0f);
								aU8 = static_cast<uint8>(a * 255.0f + 0.5f);
							}
						}
					};

				// Compute target coverage from mip0 (using integer cutoff for stability).
				const uint8 cutoffU8 = static_cast<uint8>(Clamp(alphaCutoff01, 0.0f, 1.0f) * 255.0f + 0.5f);
				const float target = ComputeCoverageU8(mips[0], cutoffU8);

				if (bytesPerPixel >= 4u && !mips.empty())
				{
					// ------------------------------------------------------------
					// Guard 1) Skip if mip0 alpha is basically opaque/constant
					// (common for opaque basecolor textures that still have an alpha channel)
					// ------------------------------------------------------------
					{
						const TextureMip& m0 = mips[0];
						const uint32 w = m0.Width;
						const uint32 h = m0.Height;

						if (w > 0 && h > 0 && !m0.Data.empty())
						{
							const uint8* data = m0.Data.data();
							const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

							uint8 aMin = 255;
							uint8 aMax = 0;

							for (uint32 y = 0; y < h; ++y)
							{
								const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
								for (uint32 x = 0; x < w; ++x)
								{
									const uint8 a = row[static_cast<size_t>(x) * bytesPerPixel + alphaByteOffset];
									aMin = std::min(aMin, a);
									aMax = std::max(aMax, a);
								}
							}

							// Loose thresholds to catch "opaque PNG with near-255 edge pixels".
							const bool alphaNearlyConstant = (aMax - aMin) <= 2; // <= 2/255
							const bool alphaNearlyOpaque = (aMin >= 250);      // min alpha >= ~0.98

							if (alphaNearlyConstant && alphaNearlyOpaque)
							{
								// Skip preserve coverage.
								goto SKIP_PRESERVE_ALPHA_COVERAGE;
							}
						}
					}

					// Guard 2) If target is effectively 0 or 1, scaling is meaningless and can still perturb alpha slightly.
					// - target ~1 => texture is almost fully opaque (should have been caught by Guard 1, but keep safe).
					// - target ~0 => almost fully masked out for this cutoff.
					if (target < 0.005f || target > 0.995f)
					{
						goto SKIP_PRESERVE_ALPHA_COVERAGE;
					}

					// For each mip > 0, find scale so that its coverage matches target.
					for (uint32 mip = 1; mip < desc.MipLevels; ++mip)
					{
						TextureMip& tm = mips[mip];

						// If mip is empty or degenerate, skip.
						if (tm.Width == 0 || tm.Height == 0 || tm.Data.empty())
							continue;

						float lo = 0.0f;
						float hi = 16.0f; // Typical upper bound; tweakable via settings if desired.

						// Optional: early widen if even hi is not enough (rare, but safe).
						// (If mip alpha collapsed close to 0 everywhere, no scale can recover coverage.)
						const float covAtHi = CoverageWithScale(tm, hi, alphaCutoff01);
						if (covAtHi < target)
						{
							// Can't reach target; best-effort at hi.
							ApplyScale(tm, hi);
							continue;
						}

						// Binary search.
						for (int it = 0; it < 10; ++it)
						{
							const float mid = 0.5f * (lo + hi);
							const float cov = CoverageWithScale(tm, mid, alphaCutoff01);

							if (cov < target)
								lo = mid;
							else
								hi = mid;
						}

						const float scale = 0.5f * (lo + hi);
						ApplyScale(tm, scale);
					}

				SKIP_PRESERVE_ALPHA_COVERAGE:
					;
				}
			}
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

