/*
 *  Copyright 2025 Diligent Graphics LLC
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

 // \file
 // Image processing tools

#include "Primitives/BasicTypes.h"


namespace shz
{
	// Image difference information
	struct ImageDiffInfo
	{
		// The number of pixels that differ
		uint32 NumDiffPixels = 0;

		// The number of pixels that differ above the threshold
		uint32 NumDiffPixelsAboveThreshold = 0;

		// The maximum difference between any two pixels
		uint32 MaxDiff = 0;

		// The average difference between all pixels, not counting pixels that are equal
		float AvgDiff = 0;

		// The root mean square difference between all pixels, not counting pixels that are equal
		float RmsDiff = 0;
	};


	// Attributes for ComputeImageDifference function
	struct ComputeImageDifferenceAttribs
	{
		// Image width
		uint32 Width = 0;

		// Image height
		uint32 Height = 0;

		// A pointer to the first image data
		const void* pImage1 = nullptr;

		// Number of channels in the first image
		uint32 NumChannels1 = 0;

		// Row stride of the first image data, in bytes
		uint32 Stride1 = 0;

		// A pointer to the second image data
		const void* pImage2 = nullptr;

		// Number of channels in the second image
		uint32 NumChannels2 = 0;

		// Row stride of the second image data, in bytes
		uint32 Stride2 = 0;

		// Difference threshold
		uint32 Threshold = 0;

		// A pointer to the difference image data.
		// If null, the difference image will not be computed.
		void* pDiffImage = nullptr;

		// Row stride of the difference image data, in bytes
		uint32 DiffStride = 0;

		// Number of channels in the difference image.
		// If 0, the number of channels will be the same as in the input images.
		uint32 NumDiffChannels = 0;

		// Scale factor for the difference image
		float Scale = 1.f;
	};

	// Computes the difference between two images
	//
	// \param [in]  Attribs    Image difference attributes, see shz::ComputeImageDifferenceAttribs.
	//
	// \return     The image difference information, see shz::ImageDiffInfo.
	//
	// The difference between two pixels is calculated as the maximum of the
	// absolute differences of all channels. The average difference is the
	// average of all differences, not counting pixels that are equal.
	// The root mean square difference is calculated as the square root of
	// the average of the squares of all differences, not counting pixels that
	// are equal.
	void ComputeImageDifference(const ComputeImageDifferenceAttribs& Attribs, ImageDiffInfo& ImageDiff);

} // namespace shz
