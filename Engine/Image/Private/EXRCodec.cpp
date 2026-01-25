#include "pch.h"
#include "Engine/Image/Public/EXRCodec.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"

#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <Imath/ImathBox.h>
#include <Imath/half.h>

namespace shz
{
	// ------------------------------------------------------------
	// Memory stream adapters for OpenEXR (InputFile/OutputFile need seek)
	// ------------------------------------------------------------

	class MemoryIStream final : public Imf::IStream
	{
	public:
		MemoryIStream(const char* name, const uint8* data, size_t size) :
			Imf::IStream{ name },
			m_Data{ data },
			m_Size{ size },
			m_Pos{ 0 }
		{
		}

		bool read(char c[], int n) override
		{
			if (n < 0)
			{
				throw std::runtime_error("MemoryIStream::read: negative size.");
			}

			const size_t nn = static_cast<size_t>(n);
			if (m_Pos + nn > m_Size)
			{
				throw std::runtime_error("MemoryIStream::read: out of range.");
			}

			memcpy(c, m_Data + m_Pos, nn);
			m_Pos += nn;
			return true;
		}

		uint64_t tellg() override { return static_cast<uint64_t>(m_Pos); }

		void seekg(uint64_t pos) override
		{
			if (pos > m_Size)
			{
				throw std::runtime_error("MemoryIStream::seekg: out of range.");
			}
			m_Pos = static_cast<size_t>(pos);
		}

		void clear() override {}

	private:
		const uint8* m_Data = nullptr;
		size_t       m_Size = 0;
		size_t       m_Pos = 0;
	};

	class MemoryOStream final : public Imf::OStream
	{
	public:
		MemoryOStream(const char* name) :
			Imf::OStream{ name }
		{
		}

		void write(const char c[], int n) override
		{
			if (n <= 0)
				return;

			const size_t nn = static_cast<size_t>(n);
			if (m_Pos + nn > m_Buffer.size())
				m_Buffer.resize(m_Pos + nn);

			memcpy(m_Buffer.data() + m_Pos, c, nn);
			m_Pos += nn;
		}

		uint64_t tellp() override { return static_cast<uint64_t>(m_Pos); }

		void seekp(uint64_t pos) override
		{
			const size_t p = static_cast<size_t>(pos);
			if (p > m_Buffer.size())
				m_Buffer.resize(p);

			m_Pos = p;
		}

		const std::vector<uint8>& GetBuffer() const { return m_Buffer; }

	private:
		std::vector<uint8> m_Buffer = {};
		size_t             m_Pos = 0;
	};

	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------

	static inline uint32 Align4(uint32 v)
	{
		return (v + 3u) & ~3u;
	}

	static inline bool HasChannel(const Imf::ChannelList& channels, const char* name)
	{
		return channels.findChannel(name) != nullptr;
	}

	static inline void FillRGBA32F_Default(float* outRGBA, int pixelCount)
	{
		// Default: (0,0,0,1)
		for (int i = 0; i < pixelCount; ++i)
		{
			outRGBA[i * 4 + 0] = 0.f;
			outRGBA[i * 4 + 1] = 0.f;
			outRGBA[i * 4 + 2] = 0.f;
			outRGBA[i * 4 + 3] = 1.f;
		}
	}

	// ------------------------------------------------------------
	// DecodeExr
	// ------------------------------------------------------------

	DECODE_EXR_RESULT DecodeEXR(
		const void* pSrcExrBits,
		size_t       ExrDataSize,
		IDataBlob* pDstPixels,
		ImageDesc* pDstImgDesc)
	{
		if (!pSrcExrBits || ExrDataSize == 0 || !pDstImgDesc)
			return DECODE_EXR_RESULT_INVALID_ARGUMENTS;

		// Signature check (EXR magic)
		// OpenEXR magic number is always 20000630 (little-endian in file).
		if (ExrDataSize < 4)
			return DECODE_EXR_RESULT_INVALID_SIGNATURE;

		{
			constexpr uint32 kExrMagic = 20000630;
			uint32 magic = 0;
			memcpy(&magic, pSrcExrBits, sizeof(uint32));
			if (magic != kExrMagic)
				return DECODE_EXR_RESULT_INVALID_SIGNATURE;
		}

		try
		{
			const uint8* bytes = reinterpret_cast<const uint8*>(pSrcExrBits);
			MemoryIStream stream("MemoryEXR", bytes, ExrDataSize);

			Imf::InputFile file(stream);
			const Imf::Header& header = file.header();

			const Imath::Box2i dw = header.dataWindow();
			const int width = dw.max.x - dw.min.x + 1;
			const int height = dw.max.y - dw.min.y + 1;

			if (width <= 0 || height <= 0)
				return DECODE_EXR_RESULT_DECODING_ERROR;

			ImageDesc desc = {};
			desc.Width = static_cast<uint32>(width);
			desc.Height = static_cast<uint32>(height);
			desc.NumComponents = 4;
			desc.ComponentType = VT_FLOAT32;

			const uint32 rowStride = Align4(static_cast<uint32>(width * 4u * sizeof(float)));
			desc.RowStride = rowStride;

			*pDstImgDesc = desc;

			if (pDstPixels == nullptr)
				return DECODE_EXR_RESULT_OK;

			pDstPixels->Resize(static_cast<size_t>(rowStride) * static_cast<size_t>(height));
			float* outRGBA = reinterpret_cast<float*>(pDstPixels->GetDataPtr(0));

			FillRGBA32F_Default(outRGBA, width * height);

			const Imf::ChannelList& channels = header.channels();

			const bool hasR = HasChannel(channels, "R");
			const bool hasG = HasChannel(channels, "G");
			const bool hasB = HasChannel(channels, "B");
			const bool hasA = HasChannel(channels, "A");
			const bool hasY = HasChannel(channels, "Y");
			const bool hasZ = HasChannel(channels, "Z");

			if (!(hasR || hasY || hasZ))
				return DECODE_EXR_RESULT_UNSUPPORTED_FORMAT;

			// Set up FrameBuffer: map EXR channels into our RGBA float buffer.
			Imf::FrameBuffer fb;

			const size_t pixelStride = 4 * sizeof(float);
			const size_t rs = static_cast<size_t>(rowStride);

			// IMPORTANT: dataWindow may not start at (0,0).
			// base pointer must be shifted so that pixel at (dw.min.x, dw.min.y) lands at outRGBA[0].
			char* base = reinterpret_cast<char*>(outRGBA);
			base -= static_cast<ptrdiff_t>(dw.min.x) * static_cast<ptrdiff_t>(pixelStride);
			base -= static_cast<ptrdiff_t>(dw.min.y) * static_cast<ptrdiff_t>(rs);

			auto InsertAsFloat = [&](const char* chName, size_t componentOffsetBytes)
				{
					fb.insert(
						chName,
						Imf::Slice(
							Imf::FLOAT,
							base + componentOffsetBytes,
							pixelStride,
							rs));
				};

			// RGB(A) path
			if (hasR) InsertAsFloat("R", 0 * sizeof(float));
			if (hasG) InsertAsFloat("G", 1 * sizeof(float));
			if (hasB) InsertAsFloat("B", 2 * sizeof(float));
			if (hasA) InsertAsFloat("A", 3 * sizeof(float));

			// Luminance-only path: Y -> RGB replicate
			if (!hasR && hasY)
			{
				InsertAsFloat("Y", 0 * sizeof(float));
				InsertAsFloat("Y", 1 * sizeof(float));
				InsertAsFloat("Y", 2 * sizeof(float));
			}

			// Depth-only path: Z -> R
			if (!hasR && hasY)
			{
				InsertAsFloat("Y", 0 * sizeof(float)); // write into R only
			}

			file.setFrameBuffer(fb);
			file.readPixels(dw.min.y, dw.max.y);

			// If we loaded only Y into R, replicate to G/B.
			if (!hasR && hasY)
			{
				for (int i = 0; i < width * height; ++i)
				{
					const float y = outRGBA[i * 4 + 0];
					outRGBA[i * 4 + 1] = y;
					outRGBA[i * 4 + 2] = y;
				}
			}

			return DECODE_EXR_RESULT_OK;
		}
		catch (...)
		{
			return DECODE_EXR_RESULT_DECODING_ERROR;
		}
	}

	// ------------------------------------------------------------
	// EncodeExr
	// ------------------------------------------------------------

	ENCODE_EXR_RESULT EncodeEXR(
		const void* pSrcPixels,
		const ImageDesc& SrcDesc,
		IDataBlob* pDstExrBits)
	{
		if (!pSrcPixels || !pDstExrBits || SrcDesc.Width == 0 || SrcDesc.Height == 0)
			return ENCODE_EXR_RESULT_INVALID_ARGUMENTS;

		if (!(SrcDesc.NumComponents == 3 || SrcDesc.NumComponents == 4))
			return ENCODE_EXR_RESULT_UNSUPPORTED_FORMAT;

		if (!(SrcDesc.ComponentType == VT_FLOAT32 || SrcDesc.ComponentType == VT_FLOAT16))
			return ENCODE_EXR_RESULT_UNSUPPORTED_FORMAT;

		try
		{
			const int width = static_cast<int>(SrcDesc.Width);
			const int height = static_cast<int>(SrcDesc.Height);

			// Output: HALF RGBA
			Imf::Header header(width, height);
			header.channels().insert("R", Imf::Channel(Imf::HALF));
			header.channels().insert("G", Imf::Channel(Imf::HALF));
			header.channels().insert("B", Imf::Channel(Imf::HALF));
			header.channels().insert("A", Imf::Channel(Imf::HALF));

			MemoryOStream stream("MemoryEXRWrite");
			Imf::OutputFile file(stream, header);

			// Convert source -> half RGBA buffer (tightly packed)
			std::vector<half> halfRGBA;
			halfRGBA.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

			const uint32 bpc =
				(SrcDesc.ComponentType == VT_FLOAT32) ? static_cast<uint32>(sizeof(float)) :
				(SrcDesc.ComponentType == VT_FLOAT16) ? static_cast<uint32>(sizeof(uint16)) : 0u;

			const uint32 srcRowStrideBytes =
				(SrcDesc.RowStride != 0) ?
				SrcDesc.RowStride :
				Align4(static_cast<uint32>(SrcDesc.Width) * static_cast<uint32>(SrcDesc.NumComponents) * bpc);

			for (int y = 0; y < height; ++y)
			{
				const uint8* srcRow = reinterpret_cast<const uint8*>(pSrcPixels) + static_cast<size_t>(y) * srcRowStrideBytes;

				for (int x = 0; x < width; ++x)
				{
					const size_t dstIdx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;

					if (SrcDesc.ComponentType == VT_FLOAT32)
					{
						const float* src = reinterpret_cast<const float*>(srcRow) + static_cast<size_t>(x) * static_cast<size_t>(SrcDesc.NumComponents);

						const float r = src[0];
						const float g = (SrcDesc.NumComponents > 1) ? src[1] : r;
						const float b = (SrcDesc.NumComponents > 2) ? src[2] : r;
						const float a = (SrcDesc.NumComponents > 3) ? src[3] : 1.f;

						halfRGBA[dstIdx + 0] = half(r);
						halfRGBA[dstIdx + 1] = half(g);
						halfRGBA[dstIdx + 2] = half(b);
						halfRGBA[dstIdx + 3] = half(a);
					}
					else // VT_FLOAT16
					{
						// Assume source float16 is IEEE half-compatible in memory (uint16 storage).
						// If you have your own half type, adjust this conversion.
						const uint16* src16 = reinterpret_cast<const uint16*>(srcRow) + static_cast<size_t>(x) * static_cast<size_t>(SrcDesc.NumComponents);

						const half r = *reinterpret_cast<const half*>(&src16[0]);
						const half g = (SrcDesc.NumComponents > 1) ? *reinterpret_cast<const half*>(&src16[1]) : r;
						const half b = (SrcDesc.NumComponents > 2) ? *reinterpret_cast<const half*>(&src16[2]) : r;
						const half a = (SrcDesc.NumComponents > 3) ? *reinterpret_cast<const half*>(&src16[3]) : half(1.f);

						halfRGBA[dstIdx + 0] = r;
						halfRGBA[dstIdx + 1] = g;
						halfRGBA[dstIdx + 2] = b;
						halfRGBA[dstIdx + 3] = a;
					}
				}
			}

			Imf::FrameBuffer fb;

			const size_t pixelStride = 4 * sizeof(half);
			const size_t rowStride = static_cast<size_t>(width) * pixelStride;

			char* base = reinterpret_cast<char*>(halfRGBA.data());

			fb.insert("R", Imf::Slice(Imf::HALF, base + 0 * sizeof(half), pixelStride, rowStride));
			fb.insert("G", Imf::Slice(Imf::HALF, base + 1 * sizeof(half), pixelStride, rowStride));
			fb.insert("B", Imf::Slice(Imf::HALF, base + 2 * sizeof(half), pixelStride, rowStride));
			fb.insert("A", Imf::Slice(Imf::HALF, base + 3 * sizeof(half), pixelStride, rowStride));

			file.setFrameBuffer(fb);
			file.writePixels(height);

			// Copy out to IDataBlob
			const std::vector<uint8>& buf = stream.GetBuffer();
			pDstExrBits->Resize(buf.size());
			memcpy(pDstExrBits->GetDataPtr(0), buf.data(), buf.size());

			return ENCODE_EXR_RESULT_OK;
		}
		catch (...)
		{
			return ENCODE_EXR_RESULT_ENCODING_ERROR;
		}
	}

} // namespace shz
