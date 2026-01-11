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
 // Definition of the shz::ITexture interface and related data structures

#include "GraphicsTypes.h"
#include "IDeviceObject.h"
#include "ITextureView.h"

namespace shz
{

	// {A64B0E60-1B5E-4CFD-B880-663A1ADCBE98}
	static constexpr INTERFACE_ID IID_Texture =
	{ 0xa64b0e60, 0x1b5e, 0x4cfd,{0xb8, 0x80, 0x66, 0x3a, 0x1a, 0xdc, 0xbe, 0x98} };


	// Miscellaneous texture flags

	// The enumeration is used by TextureDesc to describe misc texture flags
	enum MISC_TEXTURE_FLAGS : uint8
	{
		// No special flags are set
		MISC_TEXTURE_FLAG_NONE = 0u,

		// Allow automatic mipmap generation with IDeviceContext::GenerateMips()

		// \note A texture must be created with BIND_RENDER_TARGET bind flag.
		MISC_TEXTURE_FLAG_GENERATE_MIPS = 1u << 0,

		// The texture will be used as a transient framebuffer attachment.

		// Memoryless textures may only be used within a render pass in a framebuffer;
		// the corresponding subpass load operation must be CLEAR or DISCARD, and the
		// subpass store operation must be DISCARD.
		MISC_TEXTURE_FLAG_MEMORYLESS = 1u << 1,

		// For sparse textures, allow binding the same memory range in different texture
		// regions or in different sparse textures.
		MISC_TEXTURE_FLAG_SPARSE_ALIASING = 1u << 2,

		// The texture will be used as an intermediate render target for rendering with
		// texture-based variable rate shading.
		// Requires SHADING_RATE_CAP_FLAG_SUBSAMPLED_RENDER_TARGET capability.
		// 
		// \note  Copy operations are not supported for subsampled textures.
		MISC_TEXTURE_FLAG_SUBSAMPLED = 1u << 3
	};
	DEFINE_FLAG_ENUM_OPERATORS(MISC_TEXTURE_FLAGS);

	// Texture description
	struct TextureDesc : public DeviceObjectAttribs
	{
		// Texture type. See shz::RESOURCE_DIMENSION for details.
		RESOURCE_DIMENSION Type = RESOURCE_DIM_UNDEFINED;

		// Texture width, in pixels.
		uint32 Width = 0;

		// Texture height, in pixels.
		uint32 Height = 0;

		union
		{
			// For a 1D array or 2D array, number of array slices.
			// 
			// For cube maps and cube map arrays, this value must be a multiple of 6.
			// The number of cube maps in the texture is ArraySize / 6.
			uint32 ArraySize = 1;

			// For a 3D texture, number of depth slices
			uint32 Depth;
		};

		// Texture format, see shz::TEXTURE_FORMAT.

		// Use IRenderDevice::GetTextureFormatInfo() to check if format is supported.
		TEXTURE_FORMAT Format = TEX_FORMAT_UNKNOWN;

		// Number of Mip levels in the texture. Multisampled textures can only have 1 Mip level.

		// Specify 0 to create full mipmap chain.
		uint32          MipLevels = 1;

		// The number of samples.

		// Only 2D textures or 2D texture arrays can be multisampled.
		uint32          SampleCount = 1;

		// Bind flags, see shz::BIND_FLAGS for details.

		// Use IRenderDevice::GetTextureFormatInfoExt() to check which bind flags are supported.
		BIND_FLAGS      BindFlags = BIND_NONE;

		// Texture usage. See shz::USAGE for details.
		USAGE           Usage = USAGE_DEFAULT;

		// CPU access flags or 0 if no CPU access is allowed,

		// see shz::CPU_ACCESS_FLAGS for details.
		CPU_ACCESS_FLAGS CPUAccessFlags = CPU_ACCESS_NONE;

		// Miscellaneous flags, see shz::MISC_TEXTURE_FLAGS for details.
		MISC_TEXTURE_FLAGS MiscFlags = MISC_TEXTURE_FLAG_NONE;

		// Optimized clear value
		OptimizedClearValue ClearValue;

		// Defines which immediate contexts are allowed to execute commands that use this texture.

		// When `ImmediateContextMask` contains a bit at position n, the texture may be
		// used in the immediate context with index n directly (see DeviceContextDesc::ContextId).
		// It may also be used in a command list recorded by a deferred context that will be executed
		// through that immediate context.
		//
		// \remarks    Only specify these bits that will indicate those immediate contexts where the texture
		//             will actually be used. Do not set unnecessary bits as this will result in extra overhead.
		uint64 ImmediateContextMask = 1;


		constexpr TextureDesc() noexcept {}

		constexpr TextureDesc(
			const Char* _Name,
			RESOURCE_DIMENSION  _Type,
			uint32              _Width,
			uint32              _Height,
			uint32              _ArraySizeOrDepth,
			TEXTURE_FORMAT      _Format,
			uint32              _MipLevels = TextureDesc{}.MipLevels,
			uint32              _SampleCount = TextureDesc{}.SampleCount,
			USAGE               _Usage = TextureDesc{}.Usage,
			BIND_FLAGS          _BindFlags = TextureDesc{}.BindFlags,
			CPU_ACCESS_FLAGS    _CPUAccessFlags = TextureDesc{}.CPUAccessFlags,
			MISC_TEXTURE_FLAGS  _MiscFlags = TextureDesc{}.MiscFlags,
			OptimizedClearValue _ClearValue = TextureDesc{}.ClearValue,
			uint64              _ImmediateContextMask = TextureDesc{}.ImmediateContextMask) noexcept
			: DeviceObjectAttribs(_Name)
			, Type(_Type)
			, Width(_Width)
			, Height(_Height)
			, ArraySize(_ArraySizeOrDepth)
			, Format(_Format)
			, MipLevels(_MipLevels)
			, SampleCount(_SampleCount)
			, BindFlags(_BindFlags)
			, Usage(_Usage)
			, CPUAccessFlags(_CPUAccessFlags)
			, MiscFlags(_MiscFlags)
			, ClearValue(_ClearValue)
			, ImmediateContextMask(_ImmediateContextMask)
		{
		}

		constexpr uint32 ArraySizeOrDepth() const { return ArraySize; }

		// Tests if two texture descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the texture properties.
		constexpr bool operator ==(const TextureDesc& rhs)const
		{
			// Name is primarily used for debug purposes and does not affect the state.
			// It is ignored in comparison operation.
			return  // strcmp(Name, rhs.Name) == 0          &&
				Type == rhs.Type &&
				Width == rhs.Width &&
				Height == rhs.Height &&
				ArraySizeOrDepth() == rhs.ArraySizeOrDepth() &&
				Format == rhs.Format &&
				MipLevels == rhs.MipLevels &&
				SampleCount == rhs.SampleCount &&
				Usage == rhs.Usage &&
				BindFlags == rhs.BindFlags &&
				CPUAccessFlags == rhs.CPUAccessFlags &&
				MiscFlags == rhs.MiscFlags &&
				ClearValue == rhs.ClearValue &&
				ImmediateContextMask == rhs.ImmediateContextMask;
		}

		constexpr bool IsArray() const
		{
			return Type == RESOURCE_DIM_TEX_1D_ARRAY ||
				Type == RESOURCE_DIM_TEX_2D_ARRAY ||
				Type == RESOURCE_DIM_TEX_CUBE ||
				Type == RESOURCE_DIM_TEX_CUBE_ARRAY;
		}

		constexpr bool Is1D() const
		{
			return Type == RESOURCE_DIM_TEX_1D ||
				Type == RESOURCE_DIM_TEX_1D_ARRAY;
		}

		constexpr bool Is2D() const
		{
			return Type == RESOURCE_DIM_TEX_2D ||
				Type == RESOURCE_DIM_TEX_2D_ARRAY ||
				Type == RESOURCE_DIM_TEX_CUBE ||
				Type == RESOURCE_DIM_TEX_CUBE_ARRAY;
		}

		constexpr bool Is3D() const
		{
			return Type == RESOURCE_DIM_TEX_3D;
		}

		constexpr bool IsCube() const
		{
			return Type == RESOURCE_DIM_TEX_CUBE ||
				Type == RESOURCE_DIM_TEX_CUBE_ARRAY;
		}

		constexpr uint32 GetArraySize() const
		{
			return IsArray() ? ArraySize : 1u;
		}

		constexpr uint32 GetWidth() const
		{
			return Width;
		}

		constexpr uint32 GetHeight() const
		{
			return Is1D() ? 1u : Height;
		}

		constexpr uint32 GetDepth() const
		{
			return Is3D() ? Depth : 1u;
		}
	};

	// Describes data for one subresource
	struct TextureSubResData
	{
		// Pointer to the subresource data in CPU memory.

		// If provided, `pSrcBuffer` must be null
		const void* pData = nullptr;

		// Pointer to the GPU buffer that contains subresource data.

		// If provided, `pData` must be null
		struct IBuffer* pSrcBuffer = nullptr;

		// When updating data from the buffer (pSrcBuffer is not null),
		// offset from the beginning of the buffer to the data start
		uint64 SrcOffset = 0;

		// For 2D and 3D textures, row stride in bytes
		uint64 Stride = 0;

		// For 3D textures, depth slice stride in bytes.

		// On OpenGL, this must be a multiple of `Stride`
		uint64 DepthStride = 0;


		// Initializes the structure members with default values

		// Default values:
		// Member          | Default value
		// ----------------|--------------
		// pData           | nullptr
		// SrcOffset       | 0
		// Stride          | 0
		// DepthStride     | 0
		constexpr TextureSubResData() noexcept {}

		// Initializes the structure members to perform copy from the CPU memory
		constexpr TextureSubResData(const void* _pData, uint64 _Stride, uint64 _DepthStride = 0) noexcept
			: pData(_pData)
			, pSrcBuffer(nullptr)
			, SrcOffset(0)
			, Stride(_Stride)
			, DepthStride(_DepthStride)
		{
		}

		// Initializes the structure members to perform copy from the GPU buffer
		constexpr TextureSubResData(IBuffer* _pBuffer, uint64 _SrcOffset, uint64 _Stride, uint64 _DepthStride = 0) noexcept
			: pData(nullptr)
			, pSrcBuffer(_pBuffer)
			, SrcOffset(_SrcOffset)
			, Stride(_Stride)
			, DepthStride(_DepthStride)
		{
		}
	};

	// Describes the initial data to store in the texture
	struct TextureData
	{
		// A pointer to the array of the TextureSubResData elements containing
		// information about each subresource.
		TextureSubResData* pSubResources = nullptr;

		// The number of elements in `pSubResources` array.

		// `NumSubresources` must exactly match the number
		// of subresources in the texture. Otherwise an error
		// occurs.
		uint32             NumSubresources = 0;

		// Defines which device context will be used to initialize the texture.

		// The texture will be in write state after the initialization.
		// If an application uses the texture in another context afterwards, it
		// must synchronize the access to the texture using fence.
		// When null is provided, the first context enabled by ImmediateContextMask
		// will be used.
		struct IDeviceContext* pContext = nullptr;

		constexpr TextureData() noexcept {}

		constexpr TextureData(TextureSubResData* _pSubResources, uint32 _NumSubresources, IDeviceContext* _pContext = nullptr) noexcept
			: pSubResources(_pSubResources)
			, NumSubresources(_NumSubresources)
			, pContext(_pContext)
		{
		}
	};

	// Describes the data for one mapped subresource
	struct MappedTextureSubresource
	{
		// Pointer to the mapped subresource data in CPU memory.
		void* pData = nullptr;

		// Row stride in bytes.
		uint64 Stride = 0;

		// Depth slice stride in bytes.
		uint64 DepthStride = 0;

		constexpr MappedTextureSubresource() noexcept {}

		constexpr MappedTextureSubresource(
			void* _pData,
			uint64 _Stride,
			uint64 _DepthStride = 0) noexcept
			: pData(_pData)
			, Stride(_Stride)
			, DepthStride(_DepthStride)
		{
		}
	};

	// Describes the sparse texture properties
	struct SparseTextureProperties
	{
		// The size of the texture's virtual address space.
		uint64 AddressSpaceSize = 0;

		// Specifies where to bind the mip tail memory.
		// Reserved for internal use.
		uint64 MipTailOffset = 0;

		// Specifies how to calculate the mip tail offset for 2D array texture.
		// Reserved for internal use.
		uint64 MipTailStride = 0;

		// Specifies the mip tail size in bytes.

		// \note Single mip tail for a 2D array may exceed the 32-bit limit.
		uint64 MipTailSize = 0;

		// The first mip level in the mip tail that is packed as a whole into one
		// or multiple memory blocks.
		uint32 FirstMipInTail = ~0u;

		// Specifies the dimension of a tile packed into a single memory block.
		uint32 TileSize[3] = {};

		// Size of the sparse memory block, in bytes.

		// The offset in the packed mip tail, memory offset and memory size that are used in sparse
		// memory binding command must be multiples of the block size.
		//
		// If the shz::SPARSE_TEXTURE_FLAG_NONSTANDARD_BLOCK_SIZE flag is not set in the `Flags` member,
		// the block size is equal to SparseResourceProperties::StandardBlockSize.
		uint32 BlockSize = 0;

		// Flags that describe additional packing modes.
		SPARSE_TEXTURE_FLAGS Flags = SPARSE_TEXTURE_FLAG_NONE;
	};




	// Texture interface
	struct SHZ_INTERFACE ITexture : public IDeviceObject
	{
		// Returns the texture description used to create the object
		virtual const TextureDesc& GetDesc()const override = 0;

		// Creates a new texture view

		// \param [in] ViewDesc - View description. See shz::TextureViewDesc for details.
		// \param [out] ppView - Address of the memory location where the pointer to the view interface will be written to.
		//
		// To create a shader resource view addressing the entire texture, set only TextureViewDesc::ViewType
		// member of the ViewDesc parameter to shz::TEXTURE_VIEW_SHADER_RESOURCE and leave all other
		// members in their default values. Using the same method, you can create render target or depth stencil
		// view addressing the largest mip level.
		//
		// If texture view format is shz::TEX_FORMAT_UNKNOWN, the view format will match the texture format.
		// 
		// If texture view type is shz::TEXTURE_VIEW_UNDEFINED, the type will match the texture type.
		// If the number of mip levels is 0, and the view type is shader resource, the view will address all mip levels.
		//
		// For other view types it will address one mip level.
		//
		// If the number of slices is 0, all slices from FirstArraySlice or FirstDepthSlice will be referenced by the view.
		// For non-array textures, the only allowed values for the number of slices are 0 and 1.
		//
		// Texture view will contain strong reference to the texture, so the texture will not be destroyed
		// until all views are released.
		//
		// The function calls AddRef() for the created interface, so it must be released by
		// a call to Release() when it is no longer needed.
		virtual void CreateView(const TextureViewDesc& ViewDesc, ITextureView** ppView) = 0;

		// Returns the pointer to the default view.

		// \param [in] ViewType - Type of the requested view. See shz::TEXTURE_VIEW_TYPE.
		// \return Pointer to the interface
		//
		// \note The function does **not** increase the reference counter for the returned interface, so
		//       Release() **must not** be called.
		virtual ITextureView* GetDefaultView(TEXTURE_VIEW_TYPE ViewType) = 0;


		// Returns native texture handle specific to the underlying graphics API

		// \return A pointer to `ID3D11Resource` interface, for D3D11 implementation\n
		//         A pointer to `ID3D12Resource` interface, for D3D12 implementation\n
		//         `VkImage` handle, for Vulkan implementation\n
		//         GL texture name, for OpenGL implementation\n
		//         `MtlTexture`, for Metal implementation\n
		//         `WGPUTexture` for WebGPU implementation
		virtual uint64 GetNativeHandle() = 0;

		// Sets the usage state for all texture subresources.

		// This method does not perform state transition, but
		// resets the internal texture state to the given value.
		// This method should be used after the application finished
		// manually managing the texture state and wants to hand over
		// state management back to the engine.
		virtual void SetState(RESOURCE_STATE State) = 0;

		// Returns the internal texture state
		virtual RESOURCE_STATE GetState() const = 0;

		// Returns the sparse texture properties, see shz::SparseTextureProperties.
		virtual const SparseTextureProperties& GetSparseProperties() const = 0;
	};


} // namespace shz
