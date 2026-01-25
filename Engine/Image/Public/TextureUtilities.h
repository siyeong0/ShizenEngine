/*
 *  Copyright 2019-2024 Diligent Graphics LLC
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
 /// Defines texture utilities

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/Image/Public/TextureLoader.h"

namespace shz
{

	/// Parameters of the CopyPixels function.
	struct CopyPixelsAttribs
	{
		/// Texture width.
		uint32 Width = 0;

		/// Texture height.
		uint32 Height = 0;

		/// Source component size in bytes.
		uint32 SrcComponentSize = 0;

		/// A pointer to source pixels.
		const void* pSrcPixels = nullptr;

		/// Source stride in bytes.
		uint32 SrcStride = 0;

		/// Source component count.
		uint32 SrcCompCount = 0;

		/// A pointer to destination pixels.
		void* pDstPixels = nullptr;

		/// Destination component size in bytes.
		uint32 DstComponentSize = 0;

		/// Destination stride in bytes.
		uint32 DstStride = 0;

		/// Destination component count.
		uint32 DstCompCount = 0;

		/// If true, flip the image vertically.
		bool FlipVertically = false;

		/// Texture component swizzle.
		TextureComponentMapping Swizzle = TextureComponentMapping::Identity();
	};
	typedef struct CopyPixelsAttribs CopyPixelsAttribs;

	/// Copies texture pixels allowing changing the number of components.
	void CopyPixels(const CopyPixelsAttribs& Attribs);


	/// Parameters of the ExpandPixels function.
	struct ExpandPixelsAttribs
	{
		/// Source texture width.
		uint32 SrcWidth = 0;

		/// Source texture height.
		uint32 SrcHeight = 0;

		/// Texture component size in bytes.
		uint32 ComponentSize = 0;

		/// Component count.
		uint32 ComponentCount = 0;

		/// A pointer to source pixels.
		const void* pSrcPixels = nullptr;

		/// Source stride in bytes.
		uint32 SrcStride = 0;

		/// Destination texture width.
		uint32 DstWidth = 0;

		/// Destination texture height.
		uint32 DstHeight = 0;

		/// A pointer to destination pixels.
		void* pDstPixels = nullptr;

		/// Destination stride in bytes.
		uint32 DstStride = 0;
	};

	/// Expands the texture pixels by repeating the last row and column.
	void ExpandPixels(const ExpandPixelsAttribs& Attribs);


	/// Parameters of the PremultiplyAlpha function.
	struct PremultiplyAlphaAttribs
	{
		/// Texture width.
		uint32 Width = 0;

		/// Texture height.
		uint32 Height = 0;

		/// A pointer to pixels.
		void* pPixels = nullptr;

		/// Stride in bytes.
		uint32 Stride = 0;

		/// Component count.
		uint32 ComponentCount = 0;

		/// Component type.
		VALUE_TYPE ComponentType = VT_UINT8;

		/// If true, the texture is in sRGB format.
		bool IsSRGB = false;
	};

	/// Premultiplies image components with alpha in place.
	/// \note Alpha is assumed to be the last component.
	void PremultiplyAlpha(const PremultiplyAlphaAttribs& Attribs);


	/// Creates a texture from file.

	/// \param [in] FilePath    - Source file path.
	/// \param [in] TexLoadInfo - Texture loading information.
	/// \param [in] pDevice     - Render device that will be used to create the texture.
	/// \param [out] ppTexture  - Memory location where pointer to the created texture will be written.
	///
	/// \note The function is thread-safe.
	void CreateTextureFromFile(const Char* FilePath, const TextureLoadInfo& TexLoadInfo, IRenderDevice* pDevice, ITexture** ppTexture);

} // namespace shz