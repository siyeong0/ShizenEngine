#include "pch.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"

#include <array>
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
// Preserve alpha test coverage across mip chain
// (keep alpha coverage logic, replace background RGB logic to avoid white mips)
// ------------------------------------------------------------
		if (setting.bPreserveAlphaCoverage)
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
				constexpr uint32 kAlphaByteOffset = 3u;
				const uint32 bytesPerPixel = bpp; // expected 4

				auto Clamp01 = [](float v) -> float
				{
					return std::min(std::max(v, 0.0f), 1.0f);
				};

				auto ComputeAlphaMinMaxU8 = [&](const TextureMip& mip, uint8& outMin, uint8& outMax)
				{
					outMin = 255;
					outMax = 0;

					const uint32 w = mip.Width;
					const uint32 h = mip.Height;
					if (w == 0 || h == 0 || mip.Data.empty())
					{
						return;
					}

					const uint8* data = mip.Data.data();
					const uint64 rowBytes = static_cast<uint64>(w) * static_cast<uint64>(bytesPerPixel);

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

				// ------------------------------------------------------------
				// Histogram helpers (alpha coverage preserve)
				// ------------------------------------------------------------
				auto BuildAlphaHistogramU8 = [&](const TextureMip& mip, std::array<uint32, 256>& outHist)
				{
					outHist.fill(0);

					const uint32 w = mip.Width;
					const uint32 h = mip.Height;
					if (w == 0 || h == 0 || mip.Data.empty())
					{
						return;
					}

					const uint8* data = mip.Data.data();
					const uint64 rowBytes = static_cast<uint64>(w) * static_cast<uint64>(bytesPerPixel);

					for (uint32 y = 0; y < h; ++y)
					{
						const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
						for (uint32 x = 0; x < w; ++x)
						{
							const uint8 a = row[static_cast<size_t>(x) * bytesPerPixel + kAlphaByteOffset];
							outHist[a] += 1u;
						}
					}
				};

				auto BuildCumGreater = [&](const std::array<uint32, 256>& hist, std::array<uint64, 256>& outCumGreater)
				{
					outCumGreater.fill(0);

					uint64 running = 0;
					running = hist[255];
					outCumGreater[254] = running;

					for (int t = 253; t >= 0; --t)
					{
						running += static_cast<uint64>(hist[t + 1]);
						outCumGreater[static_cast<size_t>(t)] = running;
					}

					outCumGreater[255] = 0;
				};

				// ------------------------------------------------------------
				// Apply alpha scale + premul-aware RGB adjust (KEEP)
				// ------------------------------------------------------------
				auto ApplyScalePremulRGB = [&](TextureMip& mip, float scale)
				{
					const uint32 w = mip.Width;
					const uint32 h = mip.Height;
					if (w == 0 || h == 0 || mip.Data.empty())
					{
						return;
					}

					const float s = std::max(scale, 0.0f);
					const float eps = 1.0e-6f;

					uint8* data = mip.Data.data();
					const uint64 rowBytes = static_cast<uint64>(w) * static_cast<uint64>(bytesPerPixel);

					for (uint32 y = 0; y < h; ++y)
					{
						uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
						for (uint32 x = 0; x < w; ++x)
						{
							uint8* px = row + static_cast<size_t>(x) * static_cast<size_t>(bytesPerPixel);

							const float r = static_cast<float>(px[0]) / 255.0f;
							const float g = static_cast<float>(px[1]) / 255.0f;
							const float b = static_cast<float>(px[2]) / 255.0f;
							const float a0 = static_cast<float>(px[kAlphaByteOffset]) / 255.0f;

							float a1 = a0 * s;
							a1 = Clamp01(a1);

							if (a0 <= eps || a1 <= eps)
							{
								px[kAlphaByteOffset] = static_cast<uint8>(a1 * 255.0f + 0.5f);
								continue;
							}

							const float invA0 = 1.0f / std::max(a0, eps);

							const float premR0 = r * a0;
							const float premG0 = g * a0;
							const float premB0 = b * a0;

							const float premR1 = premR0 * (a1 * invA0);
							const float premG1 = premG0 * (a1 * invA0);
							const float premB1 = premB0 * (a1 * invA0);

							const float invA1 = 1.0f / std::max(a1, eps);

							float r1 = premR1 * invA1;
							float g1 = premG1 * invA1;
							float b1 = premB1 * invA1;

							r1 = Clamp01(r1);
							g1 = Clamp01(g1);
							b1 = Clamp01(b1);

							px[0] = static_cast<uint8>(r1 * 255.0f + 0.5f);
							px[1] = static_cast<uint8>(g1 * 255.0f + 0.5f);
							px[2] = static_cast<uint8>(b1 * 255.0f + 0.5f);
							px[kAlphaByteOffset] = static_cast<uint8>(a1 * 255.0f + 0.5f);
						}
					}
				};

				// ------------------------------------------------------------
				// Aggressive per-mip target coverage from mip0: block-max proxy (KEEP)
				// ------------------------------------------------------------
				auto SampleAlphaU8_Mip0 = [&](uint32 x, uint32 y) -> uint8
				{
					const TextureMip& m0 = mips[0];
					const uint32 w0 = m0.Width;
					const uint32 h0 = m0.Height;
					if (w0 == 0 || h0 == 0 || m0.Data.empty())
					{
						return 0;
					}

					x = std::min(x, w0 - 1u);
					y = std::min(y, h0 - 1u);

					const uint8* data = m0.Data.data();
					const uint64 rowBytes0 = static_cast<uint64>(w0) * static_cast<uint64>(bytesPerPixel);

					const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes0);
					return row[static_cast<size_t>(x) * bytesPerPixel + kAlphaByteOffset];
				};

				auto ComputeTargetCoverageFromMip0_BlockMaxProxy = [&](uint32 mipLevel, float cutoff01f) -> float
				{
					const TextureMip& m0 = mips[0];
					const uint32 w0 = m0.Width;
					const uint32 h0 = m0.Height;
					if (w0 == 0 || h0 == 0)
					{
						return 0.0f;
					}

					const float cutoff = Clamp01(cutoff01f);
					const float cutoffU8f = cutoff * 255.0f;

					const uint32 step = (mipLevel >= 31) ? 1u : (1u << mipLevel);
					const uint32 block = std::max(step, 1u);

					uint64 pass = 0;
					uint64 count = 0;

					for (uint32 by = 0; by < h0; by += block)
					{
						const uint32 y0 = by;
						const uint32 y1 = std::min(by + block, h0);
						const uint32 yc = y0 + (y1 - y0) / 2u;
						const uint32 yb = (y1 > 0) ? (y1 - 1u) : y0;

						for (uint32 bx = 0; bx < w0; bx += block)
						{
							const uint32 x0 = bx;
							const uint32 x1 = std::min(bx + block, w0);
							const uint32 xc = x0 + (x1 - x0) / 2u;
							const uint32 xb = (x1 > 0) ? (x1 - 1u) : x0;

							uint8 aMax = 0;
							aMax = std::max(aMax, SampleAlphaU8_Mip0(x0, y0));
							aMax = std::max(aMax, SampleAlphaU8_Mip0(xb, y0));
							aMax = std::max(aMax, SampleAlphaU8_Mip0(x0, yb));
							aMax = std::max(aMax, SampleAlphaU8_Mip0(xb, yb));
							aMax = std::max(aMax, SampleAlphaU8_Mip0(xc, yc));

							pass += (static_cast<float>(aMax) > cutoffU8f) ? 1ull : 0ull;
							++count;
						}
					}

					return (count > 0) ? (static_cast<float>(pass) / static_cast<float>(count)) : 0.0f;
				};

				// ------------------------------------------------------------
				// NEW background RGB fix: "propagate content color" instead of lerp-to-avg
				// ------------------------------------------------------------
				auto ComputeAverageRGBFromMip0_AlphaCut = [&](const TextureMip& mip0, uint8 alphaCutU8) -> float3
				{
					const uint32 w = mip0.Width;
					const uint32 h = mip0.Height;
					if (w == 0 || h == 0 || mip0.Data.empty())
					{
						return float3{ 1.0f, 1.0f, 1.0f };
					}

					const uint8* data = mip0.Data.data();
					const uint64 rowBytes = static_cast<uint64>(w) * static_cast<uint64>(bytesPerPixel);

					double sumR = 0.0;
					double sumG = 0.0;
					double sumB = 0.0;
					uint64 count = 0;

					for (uint32 y = 0; y < h; ++y)
					{
						const uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
						for (uint32 x = 0; x < w; ++x)
						{
							const uint8* px = row + static_cast<size_t>(x) * static_cast<size_t>(bytesPerPixel);
							const uint8 aU8 = px[kAlphaByteOffset];

							if (aU8 < alphaCutU8)
							{
								continue;
							}

							sumR += static_cast<double>(px[0]) / 255.0;
							sumG += static_cast<double>(px[1]) / 255.0;
							sumB += static_cast<double>(px[2]) / 255.0;
							++count;
						}
					}

					if (count == 0)
					{
						return float3{ 1.0f, 1.0f, 1.0f };
					}

					const double inv = 1.0 / static_cast<double>(count);
					return float3
					{
						static_cast<float>(sumR * inv),
						static_cast<float>(sumG * inv),
						static_cast<float>(sumB * inv)
					};
				};

				auto PropagateRGBFromSeeds = [&](TextureMip& mip, uint8 seedAlphaU8, const float3& fallbackAvgRGB01)
				{
					const uint32 w = mip.Width;
					const uint32 h = mip.Height;
					if (w == 0 || h == 0 || mip.Data.empty())
					{
						return;
					}

					const uint8 fbR = static_cast<uint8>(Clamp01(fallbackAvgRGB01.x) * 255.0f + 0.5f);
					const uint8 fbG = static_cast<uint8>(Clamp01(fallbackAvgRGB01.y) * 255.0f + 0.5f);
					const uint8 fbB = static_cast<uint8>(Clamp01(fallbackAvgRGB01.z) * 255.0f + 0.5f);

					std::vector<uint8> scratch;
					scratch.resize(mip.Data.size());

					auto Idx = [&](uint32 x, uint32 y) -> size_t
					{
						return (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * static_cast<size_t>(bytesPerPixel);
					};

					// If there are no seed pixels, fill everything with avg to avoid random white.
					{
						bool hasSeed = false;
						for (uint32 y = 0; y < h && !hasSeed; ++y)
						{
							for (uint32 x = 0; x < w; ++x)
							{
								const size_t i = Idx(x, y);
								if (mip.Data[i + kAlphaByteOffset] >= seedAlphaU8)
								{
									hasSeed = true;
									break;
								}
							}
						}

						if (!hasSeed)
						{
							uint8* data = mip.Data.data();
							const uint64 rowBytes = static_cast<uint64>(w) * static_cast<uint64>(bytesPerPixel);

							for (uint32 y = 0; y < h; ++y)
							{
								uint8* row = data + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
								for (uint32 x = 0; x < w; ++x)
								{
									uint8* px = row + static_cast<size_t>(x) * static_cast<size_t>(bytesPerPixel);
									px[0] = fbR;
									px[1] = fbG;
									px[2] = fbB;
								}
							}
							return;
						}
					}

					// Multi-pass dilation: for non-seed pixels, copy RGB from highest-alpha neighbor.
					// For tiny mips this converges fast; for bigger mips cap the passes.
					const uint32 maxDim = std::max(w, h);
					const int maxPasses = static_cast<int>(std::min<uint32>(maxDim, 16u)); // aggressive but bounded

					for (int p = 0; p < maxPasses; ++p)
					{
						std::memcpy(scratch.data(), mip.Data.data(), scratch.size());

						bool anyChange = false;

						for (uint32 y = 0; y < h; ++y)
						{
							for (uint32 x = 0; x < w; ++x)
							{
								const size_t i = Idx(x, y);
								const uint8 a0 = scratch[i + kAlphaByteOffset];

								if (a0 >= seedAlphaU8)
								{
									continue;
								}

								uint32 bestX = x;
								uint32 bestY = y;
								uint8 bestA = a0;

								for (int dy = -1; dy <= 1; ++dy)
								{
									const int yy = static_cast<int>(y) + dy;
									if (yy < 0 || yy >= static_cast<int>(h))
									{
										continue;
									}

									for (int dx = -1; dx <= 1; ++dx)
									{
										const int xx = static_cast<int>(x) + dx;
										if (xx < 0 || xx >= static_cast<int>(w))
										{
											continue;
										}

										if (dx == 0 && dy == 0)
										{
											continue;
										}

										const size_t j = Idx(static_cast<uint32>(xx), static_cast<uint32>(yy));
										const uint8 aN = scratch[j + kAlphaByteOffset];

										if (aN > bestA)
										{
											bestA = aN;
											bestX = static_cast<uint32>(xx);
											bestY = static_cast<uint32>(yy);
										}
									}
								}

								if (bestA > a0)
								{
									const size_t j = Idx(bestX, bestY);
									mip.Data[i + 0] = scratch[j + 0];
									mip.Data[i + 1] = scratch[j + 1];
									mip.Data[i + 2] = scratch[j + 2];
									anyChange = true;
								}
								else
								{
									// If neighbors don't help (very tiny mip), fall back to avg color.
									mip.Data[i + 0] = fbR;
									mip.Data[i + 1] = fbG;
									mip.Data[i + 2] = fbB;
								}
							}
						}

						if (!anyChange)
						{
							break;
						}
					}
				};

				// ------------------------------------------------------------
				// Guards
				// ------------------------------------------------------------
				bool bSkip = false;

				{
					uint8 aMin = 255, aMax = 0;
					ComputeAlphaMinMaxU8(mips[0], aMin, aMax);

					const bool alphaNearlyConstant = (aMax - aMin) <= 2;
					const bool alphaNearlyOpaque = (aMin >= 250);
					if (alphaNearlyConstant && alphaNearlyOpaque)
					{
						bSkip = true;
					}
				}

				if (!bSkip)
				{
					const float t0 = ComputeTargetCoverageFromMip0_BlockMaxProxy(0u, alphaCutoff01);
					if (t0 < 0.0025f || t0 > 0.9975f)
					{
						bSkip = true;
					}
				}

				if (!bSkip)
				{
					const float kScaleMin = 0.0f;
					const float kScaleMax = 1024.0f;

					const float cutoff = Clamp01(alphaCutoff01);
					const float cutoffU8f = cutoff * 255.0f;

					// Seed threshold for color propagation:
					// Use alphaCutoff as the seed definition (content that survives alpha test).
					const uint8 seedAlphaU8 = static_cast<uint8>(std::min(std::max(cutoffU8f, 0.0f), 255.0f) + 0.5f);

					// Avg from mip0 where alpha >= cutoff (fallback color).
					const float3 avgRGB01 = ComputeAverageRGBFromMip0_AlphaCut(mips[0], seedAlphaU8);

					// ------------------------------------------------------------
					// (A) Background RGB fix for mip0..n (mip0 included)
					//     Do this BEFORE alpha scaling so later alpha boosts won't reveal white fringes.
					// ------------------------------------------------------------
					for (uint32 mip = 0; mip < desc.MipLevels; ++mip)
					{
						TextureMip& tm = mips[mip];
						if (tm.Width == 0 || tm.Height == 0 || tm.Data.empty())
						{
							continue;
						}

						// For higher mips, seeds may vanish; propagation handles that and falls back to avg.
						PropagateRGBFromSeeds(tm, seedAlphaU8, avgRGB01);
					}

					// ------------------------------------------------------------
					// (B) Alpha coverage preservation: mip1..n only (KEEP behavior)
					// ------------------------------------------------------------
					for (uint32 mip = 1; mip < desc.MipLevels; ++mip)
					{
						TextureMip& tm = mips[mip];
						if (tm.Width == 0 || tm.Height == 0 || tm.Data.empty())
						{
							continue;
						}

						float target = ComputeTargetCoverageFromMip0_BlockMaxProxy(mip, alphaCutoff01);

						const float kBiasBase = 1.10f;
						const float kBiasSlope = 0.05f;
						const float bias = std::min(kBiasBase + kBiasSlope * static_cast<float>(mip), 2.00f);
						target = std::min(target * bias, 0.9999f);

						if (target < 0.001f || target > 0.999f)
						{
							continue;
						}

						const uint64 pixelCount = static_cast<uint64>(tm.Width) * static_cast<uint64>(tm.Height);
						if (pixelCount == 0)
						{
							continue;
						}

						std::array<uint32, 256> hist = {};
						std::array<uint64, 256> cumGreater = {};

						BuildAlphaHistogramU8(tm, hist);
						BuildCumGreater(hist, cumGreater);

						const float desiredF = std::min(std::max(target, 0.0f), 1.0f) * static_cast<float>(pixelCount);
						const uint64 desiredPass = static_cast<uint64>(std::min(desiredF + 0.999f, static_cast<float>(pixelCount - 1u)));

						if (desiredPass == 0 || desiredPass >= pixelCount)
						{
							continue;
						}

						uint32 chosenT = 0u;
						bool found = false;

						for (int t = 254; t >= 0; --t)
						{
							const uint64 pass = cumGreater[static_cast<size_t>(t)];
							if (pass >= desiredPass)
							{
								chosenT = static_cast<uint32>(t);
								found = true;
								break;
							}
						}

						if (!found)
						{
							chosenT = 0u;
						}

						if (cutoffU8f <= 0.0f)
						{
							continue;
						}

						float denom = static_cast<float>(chosenT) + 0.5f;
						denom = std::max(denom, 0.5f);

						float scale = cutoffU8f / denom;
						scale = std::min(std::max(scale, kScaleMin), kScaleMax);

						ApplyScalePremulRGB(tm, scale);

						// Optional: After alpha scaling, run a short propagation again to be extra safe
						// (alpha boosts can make previously irrelevant pixels more visible).
						// Comment out if you want it cheaper.
						PropagateRGBFromSeeds(tm, seedAlphaU8, avgRGB01);
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

