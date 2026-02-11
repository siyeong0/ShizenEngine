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
// Preserve alpha test coverage across mip chain (stronger)
// ------------------------------------------------------------
// Stronger changes:
//  1) Per-mip target coverage: compute from mip0 with stride 2^m sampling
//     so the target reflects the area that mip represents.
//  2) Auto-expand search upper bound if needed.
//  3) More iterations + early-exit on epsilon.
//  4) No goto, cleaner guards.
//
// Notes:
//  - This is a "scale only" method: alpha' = saturate(alpha * s).
//  - Works best for MASKed foliage/grass. Avoid for fully opaque textures.
        if (setting.bPreserveAlphaCoverage && desc.MipLevels > 1)
        {
            const float alphaCutoff01 = Clamp(setting.AlphaCutoff, 0.0f, 1.0f);

            bool supportedFmt = false;
            switch (outFmt)
            {
            case TEX_FORMAT_RGBA8_UNORM:
            case TEX_FORMAT_RGBA8_UNORM_SRGB:
            case TEX_FORMAT_BGRA8_UNORM:
            case TEX_FORMAT_BGRA8_UNORM_SRGB:
                supportedFmt = true;
                break;
            default:
                supportedFmt = false;
                break;
            }

            if (supportedFmt && !mips.empty() && bpp >= 4u)
            {
                constexpr uint32 kAlphaByteOffset = 3u; // RGBA/BGRA 모두 alpha는 byte 3
                const uint32 bytesPerPixel = bpp;       // expected 4

                auto Clamp01 = [](float v) -> float { return std::min(std::max(v, 0.0f), 1.0f); };

                auto ComputeAlphaMinMaxU8 = [&](const TextureMip& mip, uint8& outMin, uint8& outMax)
                {
                    outMin = 255;
                    outMax = 0;

                    const uint32 w = mip.Width;
                    const uint32 h = mip.Height;
                    if (w == 0 || h == 0 || mip.Data.empty())
                        return;

                    const uint8* data = mip.Data.data();
                    const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

                    for (uint32 y = 0; y < h; ++y)
                    {
                        const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
                        for (uint32 x = 0; x < w; ++x)
                        {
                            const uint8 a = row[static_cast<size_t>(x) * bytesPerPixel + kAlphaByteOffset];
                            outMin = std::min(outMin, a);
                            outMax = std::max(outMax, a);
                        }
                    }
                };

                auto ComputeCoverageU8 = [&](const TextureMip& mip, uint8 cutoffU8) -> float
                {
                    const uint32 w = mip.Width;
                    const uint32 h = mip.Height;
                    if (w == 0 || h == 0 || mip.Data.empty())
                        return 0.0f;

                    const uint8* data = mip.Data.data();
                    const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

                    uint64 pass = 0;
                    const uint64 count = static_cast<uint64>(w) * static_cast<uint64>(h);

                    for (uint32 y = 0; y < h; ++y)
                    {
                        const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
                        for (uint32 x = 0; x < w; ++x)
                        {
                            const uint8 a = row[static_cast<size_t>(x) * bytesPerPixel + kAlphaByteOffset];
                            pass += (a > cutoffU8) ? 1u : 0u;
                        }
                    }

                    return (count > 0) ? (static_cast<float>(pass) / static_cast<float>(count)) : 0.0f;
                };

                // Stronger: compute per-mip target coverage from mip0 using stride sampling (2^m)
                // This better matches what each mip "means" and preserves distant silhouette more reliably.
                auto ComputeTargetFromMip0_Stride = [&](uint32 mipLevel, float cutoff01f) -> float
                {
                    const TextureMip& m0 = mips[0];
                    const uint32 w0 = m0.Width;
                    const uint32 h0 = m0.Height;
                    if (w0 == 0 || h0 == 0 || m0.Data.empty())
                        return 0.0f;

                    const uint8* data = m0.Data.data();
                    const uint64 rowBytes0 = static_cast<uint64>(w0) * bytesPerPixel;

                    const uint32 step = (mipLevel >= 31) ? 0u : (1u << mipLevel);
                    const uint32 stride = std::max(step, 1u);

                    const float cutoff = Clamp01(cutoff01f);
                    const float cutoffU8f = cutoff * 255.0f;
                    const uint8 cutoffU8 = static_cast<uint8>(std::min(std::max(cutoffU8f, 0.0f), 255.0f) + 0.5f);

                    uint64 pass = 0;
                    uint64 count = 0;

                    // 샘플링은 0..w0/h0에서 stride 간격으로만 찍음
                    for (uint32 y = 0; y < h0; y += stride)
                    {
                        const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes0);
                        for (uint32 x = 0; x < w0; x += stride)
                        {
                            const uint8 a = row[static_cast<size_t>(x) * bytesPerPixel + kAlphaByteOffset];
                            pass += (a > cutoffU8) ? 1u : 0u;
                            ++count;
                        }
                    }

                    return (count > 0) ? (static_cast<float>(pass) / static_cast<float>(count)) : 0.0f;
                };

                auto CoverageWithScale = [&](const TextureMip& mip, float scale, float cutoff01f) -> float
                {
                    const uint32 w = mip.Width;
                    const uint32 h = mip.Height;
                    if (w == 0 || h == 0 || mip.Data.empty())
                        return 0.0f;

                    const uint8* data = mip.Data.data();
                    const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

                    uint64 pass = 0;
                    const uint64 count = static_cast<uint64>(w) * static_cast<uint64>(h);

                    const float cutoff = Clamp01(cutoff01f);

                    for (uint32 y = 0; y < h; ++y)
                    {
                        const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
                        for (uint32 x = 0; x < w; ++x)
                        {
                            const uint8 aU8 = row[static_cast<size_t>(x) * bytesPerPixel + kAlphaByteOffset];
                            float a = (static_cast<float>(aU8) / 255.0f) * scale;
                            a = Clamp01(a);
                            pass += (a > cutoff) ? 1u : 0u;
                        }
                    }

                    return (count > 0) ? (static_cast<float>(pass) / static_cast<float>(count)) : 0.0f;
                };

                auto ApplyScale = [&](TextureMip& mip, float scale)
                {
                    const uint32 w = mip.Width;
                    const uint32 h = mip.Height;
                    if (w == 0 || h == 0 || mip.Data.empty())
                        return;

                    uint8* data = mip.Data.data();
                    const uint64 rowBytes = static_cast<uint64>(w) * bytesPerPixel;

                    for (uint32 y = 0; y < h; ++y)
                    {
                        uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
                        for (uint32 x = 0; x < w; ++x)
                        {
                            uint8& aU8 = row[static_cast<size_t>(x) * bytesPerPixel + kAlphaByteOffset];
                            float a = (static_cast<float>(aU8) / 255.0f) * scale;
                            a = Clamp01(a);
                            aU8 = static_cast<uint8>(a * 255.0f + 0.5f);
                        }
                    }
                };

                // -----------------------------
                // Guards (strong but safe)
                // -----------------------------
                bool bSkip = false;

                // Guard 1) Skip if mip0 alpha is basically opaque/constant
                {
                    uint8 aMin = 255, aMax = 0;
                    ComputeAlphaMinMaxU8(mips[0], aMin, aMax);

                    const bool alphaNearlyConstant = (aMax - aMin) <= 2; // <= 2/255
                    const bool alphaNearlyOpaque = (aMin >= 250);      // min alpha >= ~0.98
                    if (alphaNearlyConstant && alphaNearlyOpaque)
                        bSkip = true;
                }

                // Guard 2) If mip0 target is degenerate for this cutoff, skip
                // (너무 0/1이면 scale이 의미 없고 노이즈만 만들 수 있음)
                if (!bSkip)
                {
                    const float t0 = ComputeTargetFromMip0_Stride(0, alphaCutoff01);
                    if (t0 < 0.0025f || t0 > 0.9975f)
                        bSkip = true;
                }

                if (!bSkip)
                {
                    // Tuning knobs (stronger defaults)
                    const int   kSearchIters = 16;      // 10 -> 16
                    const float kTargetEps = 1.0e-4f; // coverage 오차 허용
                    const float kScaleMin = 0.0f;
                    const float kScaleMaxHard = 128.0f;  // 16 -> 128 (하드 상한)
                    const float kScaleInitHi = 16.0f;   // 시작 hi
                    const int   kHiGrowSteps = 6;       // hi 확장 횟수 (16*2^6=1024 이지만 hard clamp 적용)

                    for (uint32 mip = 1; mip < desc.MipLevels; ++mip)
                    {
                        TextureMip& tm = mips[mip];
                        if (tm.Width == 0 || tm.Height == 0 || tm.Data.empty())
                            continue;

                        // Stronger: per-mip target
                        const float target = ComputeTargetFromMip0_Stride(mip, alphaCutoff01);

                        // target이 거의 0/1이면 해당 mip만 스킵 (여기서 억지로 만지면 오히려 튐)
                        if (target < 0.001f || target > 0.999f)
                            continue;

                        float lo = kScaleMin;
                        float hi = kScaleInitHi;

                        // Ensure hi can reach target (monotonic: scale↑ => coverage↑)
                        float covHi = CoverageWithScale(tm, hi, alphaCutoff01);
                        for (int g = 0; g < kHiGrowSteps && covHi < target && hi < kScaleMaxHard; ++g)
                        {
                            hi = std::min(hi * 2.0f, kScaleMaxHard);
                            covHi = CoverageWithScale(tm, hi, alphaCutoff01);
                        }

                        // If still cannot reach, best effort at hi.
                        if (covHi < target)
                        {
                            ApplyScale(tm, hi);
                            continue;
                        }

                        // Binary search with epsilon early-out
                        float prevMid = -1.0f;
                        for (int it = 0; it < kSearchIters; ++it)
                        {
                            const float mid = 0.5f * (lo + hi);
                            const float cov = CoverageWithScale(tm, mid, alphaCutoff01);

                            if (std::abs(cov - target) <= kTargetEps)
                            {
                                lo = hi = mid;
                                break;
                            }

                            // coverage가 target보다 작으면 scale 올려야 함
                            if (cov < target)
                                lo = mid;
                            else
                                hi = mid;

                            // scale 변화가 거의 없으면 중단
                            if (prevMid >= 0.0f && std::abs(mid - prevMid) < 1.0e-6f)
                                break;

                            prevMid = mid;
                        }

                        const float scale = 0.5f * (lo + hi);
                        ApplyScale(tm, scale);
                    }
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

