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
 // Defines shz::IBuffer interface and related data structures

#include "IDeviceObject.h"
#include "IBufferView.h"

namespace shz
{
	// {EC47EAD3-A2C4-44F2-81C5-5248D14F10E4}
	static constexpr INTERFACE_ID IID_Buffer =
	{ 0xec47ead3, 0xa2c4, 0x44f2, {0x81, 0xc5, 0x52, 0x48, 0xd1, 0x4f, 0x10, 0xe4} };

	// Describes the buffer access mode.

	// This enumeration is used by BufferDesc structure.
	enum BUFFER_MODE : uint8
	{
		// Undefined mode.
		BUFFER_MODE_UNDEFINED = 0,

		// Formatted buffer. Access to the buffer will use format conversion operations.
		// In this mode, ElementByteStride member of BufferDesc defines the buffer element size.
		// Buffer views can use different formats, but the format size must match ElementByteStride.
		BUFFER_MODE_FORMATTED,

		// Structured buffer.
		// In this mode, ElementByteStride member of BufferDesc defines the structure stride.
		BUFFER_MODE_STRUCTURED,

		// Raw buffer.
		// In this mode, the buffer is accessed as raw bytes. Formatted views of a raw
		// buffer can also be created similar to formatted buffer. If formatted views
		// are to be created, the ElementByteStride member of BufferDesc must specify the
		// size of the format.
		BUFFER_MODE_RAW,

		// Helper value storing the total number of modes in the enumeration.
		BUFFER_MODE_NUM_MODES
	};

	// Miscellaneous buffer flags.
	enum MISC_BUFFER_FLAGS : uint8
	{
		// No special flags are set.
		MISC_BUFFER_FLAG_NONE = 0,

		// For a sparse buffer, allow binding the same memory region in different buffer ranges
		// or in different sparse buffers.
		MISC_BUFFER_FLAG_SPARSE_ALIASING = 1u << 0,
	};
	DEFINE_FLAG_ENUM_OPERATORS(MISC_BUFFER_FLAGS);

	// Buffer description
	struct BufferDesc : public DeviceObjectAttribs
	{
		// Size of the buffer, in bytes. For a uniform buffer, this must be a multiple of 16.
		uint64 Size = 0;

		// Buffer bind flags, see shz::BIND_FLAGS for details

		// The following bind flags are allowed:
		// shz::BIND_VERTEX_BUFFER, shz::BIND_INDEX_BUFFER, shz::BIND_UNIFORM_BUFFER,
		// shz::BIND_SHADER_RESOURCE, shz::BIND_STREAM_OUTPUT, shz::BIND_UNORDERED_ACCESS,
		// shz::BIND_INDIRECT_DRAW_ARGS, shz::BIND_RAY_TRACING.
		// Use SparseResourceProperties::BufferBindFlags to get allowed bind flags for a sparse buffer.
		BIND_FLAGS BindFlags = BIND_NONE;

		// Buffer usage, see shz::USAGE for details
		USAGE Usage = USAGE_DEFAULT;

		// CPU access flags or 0 if no CPU access is allowed, see shz::CPU_ACCESS_FLAGS for details.
		CPU_ACCESS_FLAGS CPUAccessFlags = CPU_ACCESS_NONE;

		// Buffer mode, see shz::BUFFER_MODE
		BUFFER_MODE Mode = BUFFER_MODE_UNDEFINED;

		// Miscellaneous flags, see shz::MISC_BUFFER_FLAGS for details.
		MISC_BUFFER_FLAGS MiscFlags = MISC_BUFFER_FLAG_NONE;

		// Buffer element stride, in bytes.

		// For a structured buffer (BufferDesc::Mode equals shz::BUFFER_MODE_STRUCTURED) this member
		// defines the size of each buffer element. For a formatted buffer
		// (BufferDesc::Mode equals shz::BUFFER_MODE_FORMATTED) and optionally for a raw buffer
		// (shz::BUFFER_MODE_RAW), this member defines the size of the format that will be used for views
		// created for this buffer.
		uint32 ElementByteStride = 0;

		// Defines which immediate contexts are allowed to execute commands that use this buffer.

		// When ImmediateContextMask contains a bit at position n, the buffer may be
		// used in the immediate context with index n directly (see DeviceContextDesc::ContextId).
		// It may also be used in a command list recorded by a deferred context that will be executed
		// through that immediate context.
		//
		// \remarks    Only specify these bits that will indicate those immediate contexts where the buffer
		//             will actually be used. Do not set unnecessary bits as this will result in extra overhead.
		uint64 ImmediateContextMask = 1;


		// We have to explicitly define constructors because otherwise the following initialization fails on Apple's clang:
		//      BufferDesc{1024, BIND_UNIFORM_BUFFER, USAGE_DEFAULT}

		constexpr BufferDesc() noexcept {}

		constexpr BufferDesc(
			const Char* _Name,
			uint64 _Size,
			BIND_FLAGS _BindFlags,
			USAGE _Usage = BufferDesc{}.Usage,
			CPU_ACCESS_FLAGS _CPUAccessFlags = BufferDesc{}.CPUAccessFlags,
			BUFFER_MODE _Mode = BufferDesc{}.Mode,
			uint32 _ElementByteStride = BufferDesc{}.ElementByteStride,
			uint64 _ImmediateContextMask = BufferDesc{}.ImmediateContextMask) noexcept
			: DeviceObjectAttribs(_Name)
			, Size(_Size)
			, BindFlags(_BindFlags)
			, Usage(_Usage)
			, CPUAccessFlags(_CPUAccessFlags)
			, Mode(_Mode)
			, ElementByteStride(_ElementByteStride)
			, ImmediateContextMask(_ImmediateContextMask)
		{
		}

		// Tests if two buffer descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the buffer properties.
		constexpr bool operator == (const BufferDesc& rhs)const
		{
			// Name is primarily used for debug purposes and does not affect the state.
			// It is ignored in comparison operation.
			return  // strcmp(Name, rhs.Name) == 0          &&
				Size == rhs.Size &&
				BindFlags == rhs.BindFlags &&
				Usage == rhs.Usage &&
				CPUAccessFlags == rhs.CPUAccessFlags &&
				Mode == rhs.Mode &&
				ElementByteStride == rhs.ElementByteStride &&
				ImmediateContextMask == rhs.ImmediateContextMask;
		}
	};

	// Describes the buffer initial data
	struct BufferData
	{
		// Pointer to the data
		const void* pData = nullptr;

		// Data size, in bytes
		uint64 DataSize = 0;

		// Defines which device context will be used to initialize the buffer.

		// The buffer will be in write state after the initialization.
		// If an application uses the buffer in another context afterwards, it
		// must synchronize the access to the buffer using fence.
		// When null is provided, the first context enabled by ImmediateContextMask
		// will be used.
		struct IDeviceContext* pContext = nullptr;


		constexpr BufferData() noexcept {}

		constexpr BufferData(const void* _pData,
			uint64          _DataSize,
			IDeviceContext* _pContext = nullptr) :
			pData(_pData),
			DataSize(_DataSize),
			pContext(_pContext)
		{
		}
	};

	// Describes the sparse buffer properties
	struct SparseBufferProperties
	{
		// The size of the buffer's virtual address space.
		uint64  AddressSpaceSize = 0;

		// The size of the sparse memory block.

		// \note Offset in the buffer, memory offset and memory size that are used in sparse resource
		//       binding command, must be multiples of the block size.
		//       In Direct3D11 and Direct3D12, the block size is always 64Kb.
		//       In Vulkan, the block size is not documented, but is usually also 64Kb.
		uint32  BlockSize = 0;
	};


	// Buffer interface

	// Defines the methods to manipulate a buffer object
	struct SHZ_INTERFACE IBuffer : public IDeviceObject
	{
		// Returns the buffer description used to create the object
		virtual const BufferDesc& GetDesc() const override = 0;

		// Creates a new buffer view

		// \param [in] ViewDesc - View description. See shz::BufferViewDesc for details.
		// \param [out] ppView - Address of the memory location where the pointer to the view interface will be written to.
		//
		// \remarks To create a view addressing the entire buffer, set only BufferViewDesc::ViewType member
		//          of the ViewDesc structure and leave all other members in their default values.\n
		//          Buffer view will contain strong reference to the buffer, so the buffer will not be destroyed
		//          until all views are released.\n
		//          The function calls AddRef() for the created interface, so it must be released by
		//          a call to Release() when it is no longer needed.
		virtual void CreateView(const BufferViewDesc& ViewDesc, IBufferView** ppView) = 0;

		// Returns the pointer to the default view.

		// \param [in] ViewType - Type of the requested view. See shz::BUFFER_VIEW_TYPE.
		// \return Pointer to the interface
		//
		// \remarks Default views are only created for structured and raw buffers. As for formatted buffers
		//          the view format is unknown at buffer initialization time, no default views are created.
		//
		// \note The function does not increase the reference counter for the returned interface, so
		//       Release() must *NOT* be called.
		virtual IBufferView* GetDefaultView(BUFFER_VIEW_TYPE ViewType) = 0;

		// Returns native buffer handle specific to the underlying graphics API

		// \return A pointer to `ID3D11Resource` interface, for D3D11 implementation\n
		//         A pointer to `ID3D12Resource` interface, for D3D12 implementation\n
		//         `VkBuffer` handle, for Vulkan implementation\n
		//         GL buffer name, for OpenGL implementation\n
		//         `MtlBuffer`, for Metal implementation\n
		//         `WGPUBuffer`, for WGPU implementation\n
		virtual uint64 GetNativeHandle() = 0;

		// Sets the buffer usage state.

		// \note This method does not perform state transition, but
		//       resets the internal buffer state to the given value.
		//       This method should be used after the application finished
		//       manually managing the buffer state and wants to hand over
		//       state management back to the engine.
		virtual void SetState(RESOURCE_STATE State) = 0;

		// Returns the internal buffer state
		virtual RESOURCE_STATE GetState() const = 0;


		// Returns the buffer memory properties, see shz::MEMORY_PROPERTIES.

		// The memory properties are only relevant for persistently mapped buffers.
		// In particular, if the memory is not coherent, an application must call
		// IBuffer::FlushMappedRange() to make writes by the CPU available to the GPU, and
		// call IBuffer::InvalidateMappedRange() to make writes by the GPU visible to the CPU.
		virtual MEMORY_PROPERTIES GetMemoryProperties() const = 0;


		// Flushes the specified range of non-coherent memory from the host cache to make
		// it available to the GPU.

		// \param [in] StartOffset - Offset, in bytes, from the beginning of the buffer to
		//                           the start of the memory range to flush.
		// \param [in] Size        - Size, in bytes, of the memory range to flush.
		//
		// This method should only be used for persistently-mapped buffers that do not
		// report MEMORY_PROPERTY_HOST_COHERENT property. After an application modifies
		// a mapped memory range on the CPU, it must flush the range to make it available
		// to the GPU.
		//
		// \note   This method must never be used for USAGE_DYNAMIC buffers.
		//
		// When a mapped buffer is unmapped it is automatically flushed by
		// the engine if necessary.
		virtual void FlushMappedRange(uint64 StartOffset, uint64 Size) = 0;


		// Invalidates the specified range of non-coherent memory modified by the GPU to make
		// it visible to the CPU.

		// \param [in] StartOffset - Offset, in bytes, from the beginning of the buffer to
		//                           the start of the memory range to invalidate.
		// \param [in] Size        - Size, in bytes, of the memory range to invalidate.
		//
		// This method should only be used for persistently-mapped buffers that do not
		// report MEMORY_PROPERTY_HOST_COHERENT property. After an application modifies
		// a mapped memory range on the GPU, it must invalidate the range to make it visible
		// to the CPU.
		//
		// \note   This method must never be used for USAGE_DYNAMIC buffers.
		//
		// When a mapped buffer is unmapped, it is automatically invalidated by
		// the engine if necessary.
		virtual void InvalidateMappedRange(uint64 StartOffset, uint64 Size) = 0;

		// Returns the sparse buffer memory properties
		virtual SparseBufferProperties GetSparseProperties() const = 0;
	};


} // namespace shz
