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

 // \file
 // Definition of the shz::ISampler interface and related data structures

#include "IDeviceObject.h"

namespace shz
{

	// {595A59BF-FA81-4855-BC5E-C0E048745A95}
	static constexpr INTERFACE_ID IID_Sampler =
	{ 0x595a59bf, 0xfa81, 0x4855, {0xbc, 0x5e, 0xc0, 0xe0, 0x48, 0x74, 0x5a, 0x95} };

	// Sampler flags
	enum SAMPLER_FLAGS : uint8
	{
		// No flags are set.
		SAMPLER_FLAG_NONE = 0,

		// Specifies that the sampler will read from a subsampled texture created with MISC_TEXTURE_FLAG_SUBSAMPLED flag.
		// Requires SHADING_RATE_CAP_FLAG_SUBSAMPLED_RENDER_TARGET capability.
		SAMPLER_FLAG_SUBSAMPLED = 1u << 0,

		// Specifies that the GPU is allowed to use fast approximation when reconstructing full-resolution value from
		// the subsampled texture accessed by the sampler.
		// Requires SHADING_RATE_CAP_FLAG_SUBSAMPLED_RENDER_TARGET capability.
		SAMPLER_FLAG_SUBSAMPLED_COARSE_RECONSTRUCTION = 1u << 1,

		SAMPLER_FLAG_LAST = SAMPLER_FLAG_SUBSAMPLED_COARSE_RECONSTRUCTION
	};
	DEFINE_FLAG_ENUM_OPERATORS(SAMPLER_FLAGS);


	// Sampler description

	// This structure describes the sampler state which is used in a call to
	// IRenderDevice::CreateSampler() to create a sampler object.
	//
	// To create an anisotropic filter, all three filters must either be shz::FILTER_TYPE_ANISOTROPIC
	// or shz::FILTER_TYPE_COMPARISON_ANISOTROPIC.
	//
	// `MipFilter` cannot be comparison filter except for shz::FILTER_TYPE_ANISOTROPIC if all
	// three filters have that value.
	//
	// Both `MinFilter` and `MagFilter` must either be regular filters or comparison filters.
	// Mixing comparison and regular filters is an error.
	struct SamplerDesc : public DeviceObjectAttribs 
	{
		// Texture minification filter, see shz::FILTER_TYPE for details.

		// Default value: shz::FILTER_TYPE_LINEAR.
		FILTER_TYPE MinFilter = FILTER_TYPE_LINEAR;

		// Texture magnification filter, see shz::FILTER_TYPE for details.

		// Default value: shz::FILTER_TYPE_LINEAR.
		FILTER_TYPE MagFilter = FILTER_TYPE_LINEAR;

		// Mip filter, see shz::FILTER_TYPE for details.

		// Only FILTER_TYPE_POINT, FILTER_TYPE_LINEAR, FILTER_TYPE_ANISOTROPIC, and
		// FILTER_TYPE_COMPARISON_ANISOTROPIC are allowed.
		// Default value: shz::FILTER_TYPE_LINEAR.
		FILTER_TYPE MipFilter = FILTER_TYPE_LINEAR;

		// Texture address mode for U coordinate, see shz::TEXTURE_ADDRESS_MODE for details
		// Default value: shz::TEXTURE_ADDRESS_CLAMP.
		TEXTURE_ADDRESS_MODE AddressU = TEXTURE_ADDRESS_CLAMP;

		// Texture address mode for V coordinate, see shz::TEXTURE_ADDRESS_MODE for details

		// Default value: shz::TEXTURE_ADDRESS_CLAMP.
		TEXTURE_ADDRESS_MODE AddressV = TEXTURE_ADDRESS_CLAMP;

		// Texture address mode for W coordinate, see shz::TEXTURE_ADDRESS_MODE for details

		// Default value: shz::TEXTURE_ADDRESS_CLAMP.
		TEXTURE_ADDRESS_MODE AddressW = TEXTURE_ADDRESS_CLAMP;

		// Sampler flags, see shz::SAMPLER_FLAGS for details.
		SAMPLER_FLAGS        Flags = SAMPLER_FLAG_NONE;

		// Indicates whether to use unnormalized texture coordinates.

		// When set to `true`, the range of the image coordinates used to lookup
		// the texel is in the range of 0 to the image size in each dimension.
		// When set to `false`, the range of image coordinates is 0.0 to 1.0.
		//
		// Unnormalized coordinates are only supported in Vulkan and Metal.
		bool    UnnormalizedCoords = false;

		// Offset from the calculated mipmap level.

		// For example, if a sampler calculates that a texture should be sampled at mipmap
		// level 1.2 and MipLODBias is 2.3, then the texture will be sampled at
		// mipmap level 3.5.
		//
		// Default value: 0.
		float32 MipLODBias = 0;

		// Maximum anisotropy level for the anisotropic filter. Default value: 0.
		uint32 MaxAnisotropy = 0;

		// A function that compares sampled data against existing sampled data when comparison filter is used.

		// Default value: shz::COMPARISON_FUNC_NEVER.
		COMPARISON_FUNCTION ComparisonFunc = COMPARISON_FUNC_NEVER;

		// Border color to use if TEXTURE_ADDRESS_BORDER is specified for `AddressU`, `AddressV`, or `AddressW`.

		// Default value: `{0, 0, 0, 0}`
		float32 BorderColor[4] = {};

		// Specifies the minimum value that LOD is clamped to before accessing the texture MIP levels.

		// Must be less than or equal to `MaxLOD`.
		//
		// Default value: 0.
		float MinLOD = 0;

		// Specifies the maximum value that LOD is clamped to before accessing the texture MIP levels.

		// Must be greater than or equal to `MinLOD`.
		// Default value: `std::numeric_limits<float32>::max()`.
		float MaxLOD = +3.402823466e+38F;

		// 
		// NB: when adding new members, don't forget to update std::hash<shz::SamplerDesc>
		//

		constexpr SamplerDesc() noexcept {}

		constexpr SamplerDesc(
			FILTER_TYPE          _MinFilter,
			FILTER_TYPE          _MagFilter,
			FILTER_TYPE          _MipFilter,
			TEXTURE_ADDRESS_MODE _AddressU = SamplerDesc{}.AddressU,
			TEXTURE_ADDRESS_MODE _AddressV = SamplerDesc{}.AddressV,
			TEXTURE_ADDRESS_MODE _AddressW = SamplerDesc{}.AddressW,
			float32              _MipLODBias = SamplerDesc{}.MipLODBias,
			uint32               _MaxAnisotropy = SamplerDesc{}.MaxAnisotropy,
			COMPARISON_FUNCTION  _ComparisonFunc = SamplerDesc{}.ComparisonFunc,
			float                _MinLOD = SamplerDesc{}.MinLOD,
			float                _MaxLOD = SamplerDesc{}.MaxLOD,
			SAMPLER_FLAGS        _Flags = SamplerDesc{}.Flags,
			bool                 _UnnormalizedCoords = SamplerDesc{}.UnnormalizedCoords)
			: MinFilter(_MinFilter)
			, MagFilter(_MagFilter)
			, MipFilter(_MipFilter)
			, AddressU(_AddressU)
			, AddressV(_AddressV)
			, AddressW(_AddressW)
			, Flags(_Flags)
			, UnnormalizedCoords(_UnnormalizedCoords)
			, MipLODBias(_MipLODBias)
			, MaxAnisotropy(_MaxAnisotropy)
			, ComparisonFunc(_ComparisonFunc)
			, MinLOD(_MinLOD)
			, MaxLOD(_MaxLOD)
		{
			BorderColor[0] = BorderColor[1] = BorderColor[2] = BorderColor[3] = 0;
		}

		// Tests if two sampler descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the sampler properties.
		constexpr bool operator == (const SamplerDesc& rhs)const
		{
			// Ignore Name. This is consistent with the hasher (HashCombiner<HasherType, SamplerDesc>).
			return  // strcmp(Name, rhs.Name) == 0          &&
				MinFilter == rhs.MinFilter &&
				MagFilter == rhs.MagFilter &&
				MipFilter == rhs.MipFilter &&
				AddressU == rhs.AddressU &&
				AddressV == rhs.AddressV &&
				AddressW == rhs.AddressW &&
				Flags == rhs.Flags &&
				UnnormalizedCoords == rhs.UnnormalizedCoords &&
				MipLODBias == rhs.MipLODBias &&
				MaxAnisotropy == rhs.MaxAnisotropy &&
				ComparisonFunc == rhs.ComparisonFunc &&
				BorderColor[0] == rhs.BorderColor[0] &&
				BorderColor[1] == rhs.BorderColor[1] &&
				BorderColor[2] == rhs.BorderColor[2] &&
				BorderColor[3] == rhs.BorderColor[3] &&
				MinLOD == rhs.MinLOD &&
				MaxLOD == rhs.MaxLOD;
		}
		constexpr bool operator != (const SamplerDesc& rhs)const
		{
			return !(*this == rhs);
		}
	};


	// Texture sampler interface.

	// The interface holds the sampler state that can be used to perform texture filtering.
	// To create a sampler, call IRenderDevice::CreateSampler(). To use a sampler,
	// call ITextureView::SetSampler().
	struct SHZ_INTERFACE ISampler : public IDeviceObject
	{
		// Returns the sampler description used to create the object
		virtual const SamplerDesc& GetDesc() const override = 0;
	};

} // namespace shz
