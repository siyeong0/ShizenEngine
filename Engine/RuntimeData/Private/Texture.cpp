#include "pch.h"
#include "Engine/RuntimeData/Public/Texture.h"

namespace shz
{
	Texture Texture::ConvertGrayScale(const Texture& src)
	{
		ASSERT(src.IsValid(), "Source texture is not valid.");

		Texture dst;
		dst.SetFormat(TEX_FORMAT_R8_UNORM);

		const TEXTURE_FORMAT srcFmt = src.GetFormat();

		const bool bRGBA = (srcFmt == TEX_FORMAT_RGBA8_UNORM) || (srcFmt == TEX_FORMAT_RGBA8_UNORM_SRGB);
		const bool bBGRA = (srcFmt == TEX_FORMAT_BGRA8_UNORM) || (srcFmt == TEX_FORMAT_BGRA8_UNORM_SRGB);
		ASSERT(bRGBA || bBGRA, "Format not supported. ConvertGrayScale expects RGBA8/BGRA8 source.");

		const std::vector<TextureMip>& srcMips = src.GetMips();
		std::vector<TextureMip>& dstMips = dst.GetMips();

		dstMips.clear();
		dstMips.reserve(srcMips.size());

		for (const TextureMip& sm : srcMips)
		{
			ASSERT(sm.Width > 0 && sm.Height > 0, "Invalid mip dimensions.");
			ASSERT(sm.Data.size() >= static_cast<size_t>(sm.Width) * static_cast<size_t>(sm.Height) * 4ull,
				"Source mip data size is too small for RGBA/BGRA.");

			TextureMip dm;
			dm.Width = sm.Width;
			dm.Height = sm.Height;

			const uint32 pixelCount = dm.Width * dm.Height;

			// R8 tightly packed
			dm.Data.resize(static_cast<size_t>(pixelCount));

			const uint8* srcData = sm.Data.data();
			uint8* dstData = dm.Data.data();

			for (uint32 i = 0; i < pixelCount; ++i)
			{
				const uint8* px = srcData + static_cast<size_t>(i) * 4ull; // RGBA/BGRA = 4 bytes

				uint8 r, g, b;
				if (bRGBA)
				{
					r = px[0];
					g = px[1];
					b = px[2];
				}
				else // BGRA
				{
					b = px[0];
					g = px[1];
					r = px[2];
				}

				const uint8 gray =
					static_cast<uint8>((static_cast<uint32>(r) +
						static_cast<uint32>(g) +
						static_cast<uint32>(b)) / 3u);

				dstData[i] = gray;
			}

			dstMips.emplace_back(std::move(dm));
		}

		ASSERT(dst.IsValid(), "Destination texture is invalid.");
		return dst;
	}
} // namespace shz
