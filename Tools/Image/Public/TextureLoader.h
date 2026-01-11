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
 /// Texture loader interface.

#include "Primitives/FileStream.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/ITexture.h"
#include "Tools/Image/Public/Image.h"

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

namespace shz
{

	struct Image;
	struct IMemoryAllocator;

	/// Coarse mip filter type
	enum TEXTURE_LOAD_MIP_FILTER : uint8
	{
		/// Default filter type: BOX_AVERAGE for UNORM/SNORM and FP formats, and
		/// MOST_FREQUENT for UINT/SINT formats.
		TEXTURE_LOAD_MIP_FILTER_DEFAULT = 0,

		/// 2x2 box average.
		TEXTURE_LOAD_MIP_FILTER_BOX_AVERAGE,

		/// Use the most frequent element from the 2x2 box.
		/// This filter does not introduce new values and should be used
		/// for integer textures that contain non-filterable data (e.g. indices).
		TEXTURE_LOAD_MIP_FILTER_MOST_FREQUENT
	};

	/// Texture compression mode
	enum TEXTURE_LOAD_COMPRESS_MODE : uint8
	{
		/// Do not compress the texture.
		TEXTURE_LOAD_COMPRESS_MODE_NONE = 0,

		/// Compress the texture using BC compression.
		/// 
		/// The BC texture format is selected based on the number of channels in the
		/// source image:
		///   * `R8    -> BC4_UNORM`
		///   * `RG8   -> BC5_UNORM`
		///   * `RGB8  -> BC1_UNORM / BC1_UNORM_SRGB`
		///   * `RGBA8 -> BC3_UNORM / BC3_UNORM_SRGB`
		TEXTURE_LOAD_COMPRESS_MODE_BC,

		/// Compress the texture using high-quality BC compression.
		///
		/// This mode is similar to TEXTURE_LOAD_COMPRESS_MODE_BC, but uses higher
		/// quality settings that result in better image quality at the cost of
		/// 30%-40% longer compression time.
		TEXTURE_LOAD_COMPRESS_MODE_BC_HIGH_QUAL,
	};

	/// Texture loading information
	struct TextureLoadInfo
	{
		/// Texture name passed over to the texture creation method
		const Char* Name = nullptr;

		/// Usage
		USAGE Usage = USAGE_IMMUTABLE;

		/// Bind flags
		BIND_FLAGS BindFlags = BIND_SHADER_RESOURCE;

		/// Number of mip levels
		uint32 MipLevels = 0;

		/// CPU access flags
		CPU_ACCESS_FLAGS CPUAccessFlags = CPU_ACCESS_NONE;

		/// Flag indicating if this texture uses sRGB gamma encoding
		bool IsSRGB = false;

		/// Flag indicating that the procedure should generate lower mip levels
		bool GenerateMips = true;

		/// Flag indicating that the image should be flipped vertically
		bool FlipVertically = false;

		/// Flag indicating that RGB channels should be premultiplied by alpha
		bool PermultiplyAlpha = false;

		/// Texture format
		TEXTURE_FORMAT Format = TEX_FORMAT_UNKNOWN;

		/// Alpha cut-off value used to remap alpha channel when generating mip
		/// levels as follows:
		///
		///     A_new = max(A_old; 1/3 * A_old + 2/3 * CutoffThreshold)
		///
		/// \note This value must be in 0 to 1 range and is only
		///       allowed for 4-channel 8-bit textures.
		float AlphaCutoff = 0;

		/// Coarse mip filter type, see Diligent::TEXTURE_LOAD_MIP_FILTER.
		TEXTURE_LOAD_MIP_FILTER MipFilter = TEXTURE_LOAD_MIP_FILTER_DEFAULT;

		/// Texture compression mode, see Diligent::TEXTURE_LOAD_COMPRESS_MODE.
		TEXTURE_LOAD_COMPRESS_MODE CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		/// Texture component swizzle.

		/// When the number of channels in the source image is less than
		/// the number of channels in the destination texture, the following
		/// rules apply:
		/// - Alpha channel is always set to 1.
		/// - Single-channel source image is replicated to all channels.
		/// - Two-channel source image is replicated to RG channels, B channel is set to 0.
		TextureComponentMapping Swizzle = TextureComponentMapping::Identity();

		/// When non-zero, specifies the dimension that uniform images should be clipped to.

		/// When this parameter is non-zero, the loader will check if all pixels
		/// in the image have the same value. If this is the case, the image will
		/// be clipped to the specified dimension.
		uint32 UniformImageClipDim = 0;

		/// An optional memory allocator to allocate memory for the texture.
		struct IMemoryAllocator* pAllocator = nullptr;

		explicit TextureLoadInfo(
			const Char* _Name,
			USAGE               _Usage = TextureLoadInfo{}.Usage,
			BIND_FLAGS          _BindFlags = TextureLoadInfo{}.BindFlags,
			uint32              _MipLevels = TextureLoadInfo{}.MipLevels,
			CPU_ACCESS_FLAGS    _CPUAccessFlags = TextureLoadInfo{}.CPUAccessFlags,
			bool                _IsSRGB = TextureLoadInfo{}.IsSRGB,
			bool                _GenerateMips = TextureLoadInfo{}.GenerateMips,
			TEXTURE_FORMAT      _Format = TextureLoadInfo{}.Format)
			: Name{ _Name }
			, Usage{ _Usage }
			, BindFlags{ _BindFlags }
			, MipLevels{ _MipLevels }
			, CPUAccessFlags{ _CPUAccessFlags }
			, IsSRGB{ _IsSRGB }
			, GenerateMips{ _GenerateMips }
			, Format{ _Format }
		{
		}

		TextureLoadInfo() {};
	};

	// {E04FE6D5-8665-4183-A872-852E0F7CE242}
	static constexpr struct INTERFACE_ID IID_TextureLoader =
	{ 0xe04fe6d5, 0x8665, 0x4183, {0xa8, 0x72, 0x85, 0x2e, 0xf, 0x7c, 0xe2, 0x42} };

	// clang-format off

	/// Texture loader object.
	struct SHZ_INTERFACE ITextureLoader : public IObject
	{
		/// Creates a texture using the prepared subresource data.
		virtual void CreateTexture(IRenderDevice* pDevice, ITexture** ppTexture) = 0;

		/// Returns the texture description.
		virtual const TextureDesc& GetTextureDesc() const = 0;

		/// Returns the subresource data for the given subresource.
		virtual const TextureSubResData& GetSubresourceData(uint32 MipLevel, uint32 ArraySlice = 0) const = 0;

		/// Returns the texture initialization data.
		virtual TextureData GetTextureData() = 0;
	};

	/// Creates a texture loader from image.

	/// \param [in]  pSrcImage   - Pointer to the source image object.
	/// \param [in]  TexLoadInfo - Texture loading information, see Diligent::TextureLoadInfo.
	/// \param [out] ppLoader    - Memory location where a pointer to the created texture loader will be written.
	void CreateTextureLoaderFromImage(struct Image* pSrcImage, const TextureLoadInfo& TexLoadInfo, ITextureLoader** ppLoader);

	/// Creates a texture loader from file.

	/// \param [in]  FilePath   - File path.
	/// \param [in]  FileFormat - File format. If this parameter is IMAGE_FILE_FORMAT_UNKNOWN,
	///                           the format will be derived from the file contents.
	/// \param [in]  TexLoadInfo - Texture loading information, see Diligent::TextureLoadInfo.
	/// \param [out] ppLoader   - Memory location where a pointer to the created texture loader will be written.
	void CreateTextureLoaderFromFile(const char* FilePath, IMAGE_FILE_FORMAT FileFormat, const TextureLoadInfo& TexLoadInfo, ITextureLoader** ppLoader);

	/// Creates a texture loader from memory.

	/// \param [in]  pData       - Pointer to the texture data.
	/// \param [in]  Size        - The data size.
	/// \param [in]  MakeCopy    - Whether to make the copy of the data (see remarks).
	/// \param [in]  TexLoadInfo - Texture loading information, see Diligent::TextureLoadInfo.
	/// \param [out] ppLoader    - Memory location where a pointer to the created texture loader will be written.
	///
	/// \remarks    If MakeCopy is false, the pointer to the memory must remain valid until the
	///             texture loader object is destroyed.
	void CreateTextureLoaderFromMemory(const void* pData, size_t Size, bool MakeCopy, const TextureLoadInfo& TexLoadInfo, ITextureLoader** ppLoader);

	/// Creates a texture loader from data blob.
	///
	/// \param [in]  pDataBlob   - Pointer to the data blob that contains the texture data.
	/// \param [in]  TexLoadInfo - Texture loading information, see Diligent::TextureLoadInfo.
	/// \param [out] ppLoader    - Memory location where a pointer to the created texture loader will be written.
	///
	/// \remarks    If needed, the loader will keep a strong reference to the data blob.
	void CreateTextureLoaderFromDataBlob(IDataBlob* pDataBlob, const TextureLoadInfo& TexLoadInfo, ITextureLoader** ppLoader);

	void CreateTextureLoaderFromDataBlob(RefCntAutoPtr<IDataBlob> pDataBlob, const TextureLoadInfo& TexLoadInfo, ITextureLoader** ppLoader);


	/// Returns the memory requirement for the texture loader.
	///
	/// \param [in]  pData       - Pointer to the source image data.
	/// \param [in]  Size        - The data size.
	/// \param [in]  TexLoadInfo - Texture loading information, see Diligent::TextureLoadInfo.
	/// \return     The memory requirement in bytes.
	///
	/// This function can be used to estimate the memory requirement for the texture loader.
	/// The memory requirement includes the size of the texture data plus the size of the
	/// intermediate data structures used by the loader. It does not include the size of
	/// the source image data.
	/// The actual memory used by the loader may be slightly different.
	size_t GetTextureLoaderMemoryRequirement(const void* pData, size_t Size, const TextureLoadInfo& TexLoadInfo);


	/// Writes texture data as DDS file.

	/// \param [in]  FilePath - DDS file path.
	/// \param [in]  Desc     - Texture description.
	/// \param [in]  TexData  - Texture subresource data.
	/// \return     true if the file has been written successfully, and false otherwise.
	bool SaveTextureAsDDS(const char* FilePath, const TextureDesc& Desc, const TextureData& TexData);


	/// Writes texture as DDS to a file stream.

	/// \param [in]  pFileStream - File stream.
	/// \param [in]  Desc        - Texture description.
	/// \param [in]  TexData     - Texture subresource data.
	/// \return     true if the texture has been written successfully, and false otherwise.
	bool WriteDDSToStream(IFileStream* pFileStream, const TextureDesc& Desc, const TextureData& TexData);

} // namespace shz