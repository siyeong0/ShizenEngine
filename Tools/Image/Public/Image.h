/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

 /// \file
 /// Image loading and encoding functions.

#include <vector>

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Primitives/FileStream.h"
#include "Primitives/DataBlob.h"
#include "Primitives/BasicTypes.h"

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Common/Public/ObjectBase.hpp"

namespace shz
{
	/// Image file format
	enum IMAGE_FILE_FORMAT : uint8
	{
		/// Unknown format
		IMAGE_FILE_FORMAT_UNKNOWN = 0,

		/// The image is encoded in JPEG format
		IMAGE_FILE_FORMAT_JPEG,

		/// The image is encoded in PNG format
		IMAGE_FILE_FORMAT_PNG,

		/// The image is encoded in TIFF format
		IMAGE_FILE_FORMAT_TIFF,

		/// DDS file
		IMAGE_FILE_FORMAT_DDS,

		/// KTX file
		IMAGE_FILE_FORMAT_KTX,

		/// Silicon Graphics Image aka RGB file
		/// https://en.wikipedia.org/wiki/Silicon_Graphics_Image
		IMAGE_FILE_FORMAT_SGI,

		// HDR file
		IMAGE_FILE_FORMAT_HDR,

		// TGA file
		IMAGE_FILE_FORMAT_TGA,
	};

	/// Image loading information
	struct ImageLoadInfo
	{
		/// Image file format
		IMAGE_FILE_FORMAT Format = IMAGE_FILE_FORMAT_UNKNOWN;

		/// Whether to premultiply RGB channels by alpha
		bool PermultiplyAlpha = false;

		/// Whether the image is stored in sRGB format
		///
		/// \note This flag is only used if PermultiplyAlpha is true.
		bool IsSRGB = false;

		/// Memory allocator
		struct IMemoryAllocator* pAllocator = nullptr;
	};

	/// Image description
	struct ImageDesc
	{
		/// Image width in pixels
		uint32 Width = 0;

		/// Image height in pixels
		uint32 Height = 0;

		/// Component type
		VALUE_TYPE ComponentType = (VT_UNDEFINED);

		/// Number of color components
		uint32 NumComponents = 0;

		/// Image row stride in bytes
		uint32 RowStride = 0;
	};

	/// Implementation of a 2D image
	struct Image : public ObjectBase<IObject>
	{
		using TBase = ObjectBase<IObject>;

		/// Creates a new image from the data blob

		/// \param [in] pFileData - Pointer to the data blob containing image data
		/// \param [in] LoadInfo - Image loading information
		/// \param [out] ppImage - Memory location where pointer to the created image is written.
		///                        The image should be released via Release().
		static void CreateFromMemory(const void* pSrcData, size_t SrcDataSize, const ImageLoadInfo& LoadInfo, Image** ppImage);

		/// Creates a new image using existing pixel data
		static void CreateFromPixels(const ImageDesc& Desc, RefCntAutoPtr<IDataBlob> pPixels, Image** ppImage);

		struct EncodeInfo
		{
			uint32 Width = 0;
			uint32 Height = 0;
			TEXTURE_FORMAT TexFormat = TEX_FORMAT_UNKNOWN;
			bool KeepAlpha = false;
			bool FlipY = false;
			const void* pData = nullptr;
			uint32 Stride = 0;
			IMAGE_FILE_FORMAT FileFormat = IMAGE_FILE_FORMAT_JPEG;
			int JpegQuality = 95;
			struct IMemoryAllocator* pAllocator = nullptr;
		};
		static void Encode(const EncodeInfo& Info, IDataBlob** ppEncodedData);

		/// Returns image description
		const ImageDesc& GetDesc() const { return m_Desc; }

		/// Returns a pointer to the image data
		IDataBlob* GetData() { return m_pData; }

		const IDataBlob* GetData() const { return m_pData; }

		static std::vector<uint8> ConvertImageData(
			uint32         Width,
			uint32         Height,
			const uint8* pData,
			uint32         Stride,
			TEXTURE_FORMAT SrcFormat,
			TEXTURE_FORMAT DstFormat,
			bool           KeepAlpha,
			bool           FlipY);

		static bool IsSupportedFileFormat(IMAGE_FILE_FORMAT Format);

		static IMAGE_FILE_FORMAT GetFileFormat(const uint8* pData, size_t Size, const char* FilePath = nullptr);

		static ImageDesc GetDesc(IMAGE_FILE_FORMAT FileFormat, const void* pSrcData, size_t SrcDataSize);

		/// Returns true if the image is uniform, i.e. all pixels have the same value
		bool IsUniform() const;

	private:
		template <typename AllocatorType, typename ObjectType>
		friend class MakeNewRCObj;

		Image(IReferenceCounters* pRefCounters, const void* pSrcData, size_t SrcDataSize, const ImageLoadInfo& LoadInfo);

		Image(IReferenceCounters* pRefCounters, const ImageDesc& Desc, RefCntAutoPtr<IDataBlob> pPixels);

		static bool Load(IMAGE_FILE_FORMAT FileFormat, const void* pSrcData, size_t SrcDataSize, IDataBlob* pDstPixels, ImageDesc& Desc);

		static void LoadTiffFile(const void* pData, size_t Size, IDataBlob* pDstPixels, ImageDesc& Desc);

		ImageDesc m_Desc;
		RefCntAutoPtr<IDataBlob> m_pData;
	};

	/// Creates an image from file

	/// \param [in] FilePath   - Source file path
	/// \param [out] ppImage   - Memory location where pointer to the created image will be stored
	/// \param [out] ppRawData - If the file format is not recognized by the function, it will load raw bytes
	///                          and return them in the data blob. This parameter can be null.
	/// \return                  Image file format.
	IMAGE_FILE_FORMAT CreateImageFromFile(const Char* FilePath, Image** ppImage, IDataBlob** ppRawData = nullptr);


	/// Creates an image from memory

	/// \param [in] pImageData - Source image data
	/// \param [in] DataSize   - Size of the image data
	/// \param [out] ppImage   - Memory location where pointer to the created image will be stored
	/// \return                  Image file format.
	IMAGE_FILE_FORMAT CreateImageFromMemory(const void* pImageData, size_t      DataSize, Image** ppImage);

} // namespace shz
