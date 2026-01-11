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
 // Definition of the shz::ITextureView interface and related data structures

#include "Primitives/FlagEnum.h"
#include "IDeviceObject.h"

namespace shz
{

	struct ISampler;

	// {5B2EA04E-8128-45E4-AA4D-6DC7E70DC424}
	static constexpr INTERFACE_ID IID_TextureView =
	{ 0x5b2ea04e, 0x8128, 0x45e4,{0xaa, 0x4d, 0x6d, 0xc7, 0xe7, 0xd, 0xc4, 0x24} };


	// Describes allowed unordered access view mode
	enum UAV_ACCESS_FLAG : uint8
	{
		// Access mode is unspecified
		UAV_ACCESS_UNSPECIFIED = 0x00,

		// Allow read operations on the UAV
		UAV_ACCESS_FLAG_READ = 0x01,

		// Allow write operations on the UAV
		UAV_ACCESS_FLAG_WRITE = 0x02,

		// Allow read and write operations on the UAV
		UAV_ACCESS_FLAG_READ_WRITE = UAV_ACCESS_FLAG_READ | UAV_ACCESS_FLAG_WRITE,

		UAV_ACCESS_FLAG_LAST = UAV_ACCESS_FLAG_READ_WRITE
	};
	DEFINE_FLAG_ENUM_OPERATORS(UAV_ACCESS_FLAG);

	// Texture view flags
	enum TEXTURE_VIEW_FLAGS : uint8
	{
		// No flags
		TEXTURE_VIEW_FLAG_NONE = 0,

		// Allow automatic mipmap generation for this view.
		// This flag is only allowed for TEXTURE_VIEW_SHADER_RESOURCE view type.
		// The texture must be created with MISC_TEXTURE_FLAG_GENERATE_MIPS flag.
		TEXTURE_VIEW_FLAG_ALLOW_MIP_MAP_GENERATION = 1u << 0,

		TEXTURE_VIEW_FLAG_LAST = TEXTURE_VIEW_FLAG_ALLOW_MIP_MAP_GENERATION
	};
	DEFINE_FLAG_ENUM_OPERATORS(TEXTURE_VIEW_FLAGS);


	// Texture component swizzle
	enum TEXTURE_COMPONENT_SWIZZLE : uint8
	{
		// Identity swizzle (e.g. `R->R`, `G->G`, `B->B`, `A->A`).
		TEXTURE_COMPONENT_SWIZZLE_IDENTITY = 0,

		// The component is set to zero.
		TEXTURE_COMPONENT_SWIZZLE_ZERO,

		// The component is set to one.
		TEXTURE_COMPONENT_SWIZZLE_ONE,

		// The component is set to the value of the red channel of the texture.
		TEXTURE_COMPONENT_SWIZZLE_R,

		// The component is set to the value of the green channel of the texture.
		TEXTURE_COMPONENT_SWIZZLE_G,

		// The component is set to the value of the blue channel of the texture.
		TEXTURE_COMPONENT_SWIZZLE_B,

		// The component is set to the value of the alpha channel of the texture.
		TEXTURE_COMPONENT_SWIZZLE_A,

		TEXTURE_COMPONENT_SWIZZLE_COUNT
	};


	// Defines the per-channel texutre component mapping.
	struct TextureComponentMapping
	{
		// Defines the component placed in the red component of the output vector.
		TEXTURE_COMPONENT_SWIZZLE R = TEXTURE_COMPONENT_SWIZZLE_IDENTITY;

		// Defines the component placed in the green component of the output vector.
		TEXTURE_COMPONENT_SWIZZLE G = TEXTURE_COMPONENT_SWIZZLE_IDENTITY;

		// Defines the component placed in the blue component of the output vector.
		TEXTURE_COMPONENT_SWIZZLE B = TEXTURE_COMPONENT_SWIZZLE_IDENTITY;

		// Defines the component placed in the alpha component of the output vector.
		TEXTURE_COMPONENT_SWIZZLE A = TEXTURE_COMPONENT_SWIZZLE_IDENTITY;

		constexpr TextureComponentMapping() noexcept {}

		constexpr TextureComponentMapping(
			TEXTURE_COMPONENT_SWIZZLE _R,
			TEXTURE_COMPONENT_SWIZZLE _G,
			TEXTURE_COMPONENT_SWIZZLE _B,
			TEXTURE_COMPONENT_SWIZZLE _A) noexcept
			: R(_R)
			, G(_G)
			, B(_B)
			, A(_A)
		{
		}

		constexpr uint32 Asuint32() const
		{
			return (static_cast<uint32>(R) << 0u) |
				(static_cast<uint32>(G) << 8u) |
				(static_cast<uint32>(B) << 16u) |
				(static_cast<uint32>(A) << 24u);
		}

		constexpr bool operator==(const TextureComponentMapping& rhs) const
		{
			return (R == rhs.R || (R == TEXTURE_COMPONENT_SWIZZLE_IDENTITY && rhs.R == TEXTURE_COMPONENT_SWIZZLE_R) || (R == TEXTURE_COMPONENT_SWIZZLE_R && rhs.R == TEXTURE_COMPONENT_SWIZZLE_IDENTITY)) &&
				(G == rhs.G || (G == TEXTURE_COMPONENT_SWIZZLE_IDENTITY && rhs.G == TEXTURE_COMPONENT_SWIZZLE_G) || (G == TEXTURE_COMPONENT_SWIZZLE_G && rhs.G == TEXTURE_COMPONENT_SWIZZLE_IDENTITY)) &&
				(B == rhs.B || (B == TEXTURE_COMPONENT_SWIZZLE_IDENTITY && rhs.B == TEXTURE_COMPONENT_SWIZZLE_B) || (B == TEXTURE_COMPONENT_SWIZZLE_B && rhs.B == TEXTURE_COMPONENT_SWIZZLE_IDENTITY)) &&
				(A == rhs.A || (A == TEXTURE_COMPONENT_SWIZZLE_IDENTITY && rhs.A == TEXTURE_COMPONENT_SWIZZLE_A) || (A == TEXTURE_COMPONENT_SWIZZLE_A && rhs.A == TEXTURE_COMPONENT_SWIZZLE_IDENTITY));
		}
		constexpr bool operator!=(const TextureComponentMapping& rhs) const
		{
			return !(*this == rhs);
		}

		constexpr TEXTURE_COMPONENT_SWIZZLE operator[](size_t Component) const
		{
			return (&R)[Component];
		}

		constexpr TEXTURE_COMPONENT_SWIZZLE& operator[](size_t Component)
		{
			return (&R)[Component];
		}

		static constexpr TextureComponentMapping Identity()
		{
			return {
				TEXTURE_COMPONENT_SWIZZLE_IDENTITY,
				TEXTURE_COMPONENT_SWIZZLE_IDENTITY,
				TEXTURE_COMPONENT_SWIZZLE_IDENTITY,
				TEXTURE_COMPONENT_SWIZZLE_IDENTITY
			};
		}

		// Combines two component mappings into one.
		// The resulting mapping is equivalent to first applying the first (lhs) mapping,
		// then applying the second (rhs) mapping.
		TextureComponentMapping operator*(const TextureComponentMapping& rhs) const
		{
			TextureComponentMapping CombinedMapping;
			for (size_t c = 0; c < 4; ++c)
			{
				TEXTURE_COMPONENT_SWIZZLE  rhsCompSwizzle = rhs[c];
				TEXTURE_COMPONENT_SWIZZLE& DstCompSwizzle = CombinedMapping[c];
				switch (rhsCompSwizzle)
				{
				case TEXTURE_COMPONENT_SWIZZLE_IDENTITY: DstCompSwizzle = (*this)[c]; break;
				case TEXTURE_COMPONENT_SWIZZLE_ZERO:     DstCompSwizzle = TEXTURE_COMPONENT_SWIZZLE_ZERO; break;
				case TEXTURE_COMPONENT_SWIZZLE_ONE:      DstCompSwizzle = TEXTURE_COMPONENT_SWIZZLE_ONE; break;
				case TEXTURE_COMPONENT_SWIZZLE_R:        DstCompSwizzle = (R == TEXTURE_COMPONENT_SWIZZLE_IDENTITY) ? TEXTURE_COMPONENT_SWIZZLE_R : R; break;
				case TEXTURE_COMPONENT_SWIZZLE_G:        DstCompSwizzle = (G == TEXTURE_COMPONENT_SWIZZLE_IDENTITY) ? TEXTURE_COMPONENT_SWIZZLE_G : G; break;
				case TEXTURE_COMPONENT_SWIZZLE_B:        DstCompSwizzle = (B == TEXTURE_COMPONENT_SWIZZLE_IDENTITY) ? TEXTURE_COMPONENT_SWIZZLE_B : B; break;
				case TEXTURE_COMPONENT_SWIZZLE_A:        DstCompSwizzle = (A == TEXTURE_COMPONENT_SWIZZLE_IDENTITY) ? TEXTURE_COMPONENT_SWIZZLE_A : A; break;
				default: DstCompSwizzle = (*this)[c]; break;
				}

				if ((DstCompSwizzle == TEXTURE_COMPONENT_SWIZZLE_R && c == 0) ||
					(DstCompSwizzle == TEXTURE_COMPONENT_SWIZZLE_G && c == 1) ||
					(DstCompSwizzle == TEXTURE_COMPONENT_SWIZZLE_B && c == 2) ||
					(DstCompSwizzle == TEXTURE_COMPONENT_SWIZZLE_A && c == 3))
				{
					DstCompSwizzle = TEXTURE_COMPONENT_SWIZZLE_IDENTITY;
				}
			}
			static_assert(TEXTURE_COMPONENT_SWIZZLE_COUNT == 7, "Please handle the new component swizzle");
			return CombinedMapping;
		}

		TextureComponentMapping& operator*=(const TextureComponentMapping& rhs)
		{
			*this = *this * rhs;
			return *this;
		}
	};


	// Texture view description
	struct TextureViewDesc : public DeviceObjectAttribs
	{
		// Describes the texture view type, see shz::TEXTURE_VIEW_TYPE for details.
		TEXTURE_VIEW_TYPE ViewType = TEXTURE_VIEW_UNDEFINED;

		// View interpretation of the original texture.

		// For instance, one slice of a 2D texture array can be viewed as a 2D texture.
		// See shz::RESOURCE_DIMENSION for a list of texture types.
		// If default value shz::RESOURCE_DIM_UNDEFINED is provided,
		// the view type will match the type of the referenced texture.
		RESOURCE_DIMENSION TextureDim = RESOURCE_DIM_UNDEFINED;

		// View format.

		// If default value shz::TEX_FORMAT_UNKNOWN is provided,
		// the view format will match the referenced texture format.
		TEXTURE_FORMAT Format = TEX_FORMAT_UNKNOWN;

		// Most detailed mip level to use
		uint32 MostDetailedMip = 0;

		// Total number of mip levels for the view of the texture.

		// Render target and depth stencil views can address only one mip level.
		// If 0 is provided, then for a shader resource view all mip levels will be
		// referenced, and for a render target or a depth stencil view, one mip level
		// will be referenced.
		uint32 NumMipLevels = 0;

		union
		{
			// For a texture array, first array slice to address in the view
			uint32 FirstArraySlice = 0;

			// For a 3D texture, first depth slice to address the view
			uint32 FirstDepthSlice;
		};

		union
		{
			// For a texture array, number of array slices to address in the view.

			// Set to 0 to address all array slices.
			uint32 NumArraySlices = 0;

			// For a 3D texture, number of depth slices to address in the view

			// Set to 0 to address all depth slices.
			uint32 NumDepthSlices;
		};

		// For an unordered access view, allowed access flags.

		// See shz::UAV_ACCESS_FLAG for details.
		UAV_ACCESS_FLAG    AccessFlags = UAV_ACCESS_UNSPECIFIED;

		// Texture view flags, see shz::TEXTURE_VIEW_FLAGS.
		TEXTURE_VIEW_FLAGS Flags = TEXTURE_VIEW_FLAG_NONE;

		// Texture component swizzle, see shz::TextureComponentMapping.
		TextureComponentMapping Swizzle;

		// 
		// NB: when adding new members, don't forget to update std::hash<shz::TextureViewDesc>
		//
		constexpr TextureViewDesc() noexcept {}

		constexpr TextureViewDesc(const Char* _Name,
			TEXTURE_VIEW_TYPE  _ViewType,
			RESOURCE_DIMENSION _TextureDim,
			TEXTURE_FORMAT     _Format = TextureViewDesc{}.Format,
			uint32             _MostDetailedMip = TextureViewDesc{}.MostDetailedMip,
			uint32             _NumMipLevels = TextureViewDesc{}.NumMipLevels,
			uint32             _FirstArrayOrDepthSlice = TextureViewDesc{}.FirstArraySlice,
			uint32             _NumArrayOrDepthSlices = TextureViewDesc{}.NumArraySlices,
			UAV_ACCESS_FLAG    _AccessFlags = TextureViewDesc{}.AccessFlags,
			TEXTURE_VIEW_FLAGS _Flags = TextureViewDesc{}.Flags) noexcept
			: DeviceObjectAttribs(_Name)
			, ViewType(_ViewType)
			, TextureDim(_TextureDim)
			, Format(_Format)
			, MostDetailedMip(_MostDetailedMip)
			, NumMipLevels(_NumMipLevels)
			, FirstArraySlice(_FirstArrayOrDepthSlice)
			, NumArraySlices(_NumArrayOrDepthSlices)
			, AccessFlags(_AccessFlags)
			, Flags(_Flags)
		{
		}

		constexpr uint32 FirstArrayOrDepthSlice() const { return FirstArraySlice; }
		constexpr uint32 NumArrayOrDepthSlices()  const { return NumArraySlices; }

		// Tests if two texture view descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the texture view properties.
		constexpr bool operator==(const TextureViewDesc& rhs) const
		{
			// Ignore Name. This is consistent with the hasher (HashCombiner<HasherType, TextureViewDesc>).
			return //strcmp(Name, rhs.Name) == 0            &&
				ViewType == rhs.ViewType &&
				TextureDim == rhs.TextureDim &&
				Format == rhs.Format &&
				MostDetailedMip == rhs.MostDetailedMip &&
				NumMipLevels == rhs.NumMipLevels &&
				FirstArrayOrDepthSlice() == rhs.FirstArrayOrDepthSlice() &&
				NumArrayOrDepthSlices() == rhs.NumArrayOrDepthSlices() &&
				AccessFlags == rhs.AccessFlags &&
				Flags == rhs.Flags &&
				Swizzle == rhs.Swizzle;
		}
		constexpr bool operator!=(const TextureViewDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};






	// Texture view interface

	// To create a texture view, call ITexture::CreateView().
	// Texture view holds strong references to the texture. The texture
	// will not be destroyed until all views are released.
	// The texture view will also keep a strong reference to the texture sampler,
	// if any is set.
	struct SHZ_INTERFACE ITextureView : public IDeviceObject
	{
		// Returns the texture view description used to create the object
		virtual const TextureViewDesc& GetDesc() const override = 0;

		// Sets the texture sampler to use for filtering operations
		// when accessing a texture from shaders. Only
		// shader resource views can be assigned a sampler.
		// The view will keep strong reference to the sampler.
		virtual void SetSampler(struct ISampler* pSampler) = 0;

		// Returns the pointer to the sampler object set by the ITextureView::SetSampler().

		// The method does **NOT** increment the reference counter of the returned object,
		// so Release() **must not** be called.
		virtual struct ISampler* GetSampler() = 0;


		// Returns a pointer to the referenced texture object.

		// The method does **NOT** increment the reference counter of the returned object,
		// so Release() **must not** be called.
		virtual struct ITexture* GetTexture() = 0;
	};


}
