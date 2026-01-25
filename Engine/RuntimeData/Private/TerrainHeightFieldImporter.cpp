#include "pch.h"
#include "Engine/RuntimeData/Public/TerrainHeightFieldImporter.h"

#include "Engine/Image/Public/TextureLoader.h"
#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"
#include "Engine/AssetManager/Public/AssetTypeTraits.h"

namespace shz
{
	static inline void setError(std::string* pOutError, const std::string& msg)
	{
		if (pOutError)
		{
			*pOutError = msg;
		}
	}

	static inline uint32 getNumComponentsFromFormat(TEXTURE_FORMAT fmt)
	{
		const TextureFormatAttribs& a = GetTextureFormatAttribs(fmt);
		return a.NumComponents;
	}

	static inline uint32 getComponentSizeFromFormat(TEXTURE_FORMAT fmt)
	{
		const TextureFormatAttribs& a = GetTextureFormatAttribs(fmt);
		return a.ComponentSize;
	}

	// Read "R" from a pixel with arbitrary component count.
	// Assumes tightly packed component array in a pixel.
	static inline uint8 readR_U8(const uint8* pPixel) noexcept { return pPixel[0]; }
	static inline uint16 readR_U16(const uint16* pPixel) noexcept { return pPixel[0]; }
	static inline float readR_F32(const float* pPixel) noexcept { return pPixel[0]; }

	std::unique_ptr<AssetObject> TerrainHeightFieldImporter::operator()(
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
			ASSERT(false, "meta.SourcePath is empty.");
			setError(pOutError, "SourcePath is empty.");
			return {};
		}

		const TerrainHeightFieldImportSetting setting = meta.TryGetTerrainHeightFieldMeta() ? *meta.TryGetTerrainHeightFieldMeta() : TerrainHeightFieldImportSetting{};

		// ------------------------------------------------------------
		// Build TextureLoadInfo (system memory, 1-channel preferred)
		// ------------------------------------------------------------
		TextureLoadInfo tli = {};
		tli.Name = meta.Name.empty() ? "TerrainHeightField" : meta.Name.c_str();

		// Heightmap is data: no sRGB, no compression, no mips for CPU HF
		tli.IsSRGB = false;
		tli.GenerateMips = false;
		tli.FlipVertically = false;   // 필요하면 meta로 노출
		tli.PremultiplyAlpha = false;

		tli.MipFilter = TEXTURE_LOAD_MIP_FILTER_DEFAULT;
		tli.CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		tli.Swizzle = TextureComponentMapping::Identity();
		tli.UniformImageClipDim = 0;

		// Force loader output format if possible:
		// - Prefer R16 if the source is 16-bit heightmap
		// - Otherwise R8 is fine
		// - If your loader supports R32_FLOAT and you want float pipeline, you can switch.
		//
		// We don't know the source bit depth before loading, so:
		// 1) Try R16 first (most heightmaps are 16-bit)
		// 2) Fallback to R8 if loader fails or returns something unexpected
		// tli.Format = TEX_FORMAT_R16_UNORM;

		RefCntAutoPtr<ITextureLoader> pLoader;
		CreateTextureLoaderFromFile(meta.SourcePath.c_str(), IMAGE_FILE_FORMAT_UNKNOWN, tli, &pLoader);

		if (!pLoader)
		{
			// Fallback: try R8
			tli.Format = TEX_FORMAT_R8_UNORM;
			CreateTextureLoaderFromFile(meta.SourcePath.c_str(), IMAGE_FILE_FORMAT_UNKNOWN, tli, &pLoader);
		}


		ASSERT(!!pLoader, "Failed to create texture loader for heightfield.");

		const TextureDesc& desc = pLoader->GetTextureDesc();
		ASSERT(desc.Width > 0 && desc.Height > 0, "Invalid texture dimensions from loader.");
		ASSERT(desc.MipLevels > 0, "Invalid mip levels from loader.");

		// We only need base mip for heightfield CPU data.
		const TextureSubResData& sub = pLoader->GetSubresourceData(/*MipLevel*/0, /*ArraySlice*/0);
		ASSERT(sub.pData != nullptr && sub.Stride > 0, "Invalid subresource data from loader.");

		const TEXTURE_FORMAT fmt = desc.Format;
		const uint32 numComps = getNumComponentsFromFormat(fmt);
		const uint32 compSize = getComponentSizeFromFormat(fmt);
		ASSERT(numComps > 0 && compSize > 0, "Invalid texture format from loader.");

		// ------------------------------------------------------------
		// Create TerrainHeightField (always store float normalized)
		// ------------------------------------------------------------
		TerrainHeightFieldCreateInfo ci = {};
		ci.Width = desc.Width;
		ci.Height = desc.Height;

		ci.WorldSpacingX = setting.WorldSpacingX;
		ci.WorldSpacingZ = setting.WorldSpacingZ;

		ci.HeightScale = setting.HeightScale;
		ci.HeightOffset = setting.HeightOffset;

		// For debug bookkeeping only (actual storage is float)
		ci.SampleFormat = setting.ForceSampleFormat;
		if (ci.SampleFormat == HEIGHT_FIELD_SAMPLE_FORMAT_UNKNOWN)
		{
			// Guess from component size
			if (compSize == 1)      ci.SampleFormat = HEIGHT_FIELD_SAMPLE_FORMAT_UINT8;
			else if (compSize == 2) ci.SampleFormat = HEIGHT_FIELD_SAMPLE_FORMAT_UINT16;
			else                    ci.SampleFormat = HEIGHT_FIELD_SAMPLE_FORMAT_FLOAT32;
		}

		TerrainHeightField hf = {};
		hf.Initialize(ci);

		const uint32 width = desc.Width;
		const uint32 height = desc.Height;

		// stride in bytes from loader; per-pixel bytes:
		const uint32 bytesPerPixel = numComps * compSize;

		// Minimal sanity
		const uint64 minRowBytes = static_cast<uint64>(width) * static_cast<uint64>(bytesPerPixel);
		ASSERT(sub.Stride >= minRowBytes, "Source stride is smaller than tightly packed row size.");

		// ------------------------------------------------------------
		// Convert pixels -> normalized float [0..1] stored in hf
		// We use only R channel (heightmap grayscale => R==G==B)
		// ------------------------------------------------------------
		const uint8* srcBase = reinterpret_cast<const uint8*>(sub.pData);
		const uint64 srcStride = sub.Stride;

		if (compSize == 1)
		{
			const float inv = 1.f / 255.f;

			for (uint32 z = 0; z < height; ++z)
			{
				const uint8* row = srcBase + static_cast<size_t>(z) * static_cast<size_t>(srcStride);
				for (uint32 x = 0; x < width; ++x)
				{
					const uint8* px = row + static_cast<size_t>(x) * bytesPerPixel;
					const uint8 r = readR_U8(px);
					const float n = Clamp01(static_cast<float>(r) * inv);
					hf.SetNormalizedHeightAt(x, z, n);
				}
			}
		}
		else if (compSize == 2)
		{
			const float inv = 1.f / 65535.f;

			for (uint32 z = 0; z < height; ++z)
			{
				const uint8* rowBytes = srcBase + static_cast<size_t>(z) * static_cast<size_t>(srcStride);
				for (uint32 x = 0; x < width; ++x)
				{
					const uint16* px = reinterpret_cast<const uint16*>(rowBytes + static_cast<size_t>(x) * bytesPerPixel);
					const uint16 r = readR_U16(px);
					const float n = Clamp01(static_cast<float>(r) * inv);
					hf.SetNormalizedHeightAt(x, z, n);
				}
			}
		}
		else if (compSize == 4)
		{
			for (uint32 z = 0; z < height; ++z)
			{
				const uint8* rowBytes = srcBase + static_cast<size_t>(z) * static_cast<size_t>(srcStride);
				for (uint32 x = 0; x < width; ++x)
				{
					const float* px = reinterpret_cast<const float*>(rowBytes + static_cast<size_t>(x) * bytesPerPixel);
					const float r = readR_F32(px);
					hf.SetNormalizedHeightAt(x, z, Clamp01(r));
				}
			}
		}
		else
		{
			ASSERT(false, "Unsupported component size.");
			setError(pOutError, "Unsupported component size.");
			return {};
		}

		// Resident bytes (CPU height data)
		*pOutResidentBytes = static_cast<uint64>(hf.GetData().size() * sizeof(float));
		return std::make_unique<TypedAssetObject<TerrainHeightField>>(hf);
	}

} // namespace shz
