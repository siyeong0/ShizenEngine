/*
 *  Copyright 2019-2025 Diligent Graphics LLC
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
 // Definition of the shz::IDeviceMemory interface

#include "IDeviceObject.h"

namespace shz
{

	// {815F7AE1-84A8-4ADD-A93B-3E28C1711D5E}
	static constexpr INTERFACE_ID IID_DeviceMemory =
	{ 0x815f7ae1, 0x84a8, 0x4add, {0xa9, 0x3b, 0x3e, 0x28, 0xc1, 0x71, 0x1d, 0x5e} };

	// Describes the device memory type.

	// This enumeration is used by DeviceMemoryDesc structure.
	enum DEVICE_MEMORY_TYPE : uint8
	{
		// Indicates that the memory type is not defined.
		DEVICE_MEMORY_TYPE_UNDEFINED = 0,

		// Indicates that memory will be used for sparse resources.
		DEVICE_MEMORY_TYPE_SPARSE = 1,
	};

	// Device memory description
	struct DeviceMemoryDesc : public DeviceObjectAttribs
	{
		// Memory type, see shz::DEVICE_MEMORY_TYPE.
		DEVICE_MEMORY_TYPE  Type = DEVICE_MEMORY_TYPE_UNDEFINED;

		// Size of the memory page, in bytes.

		// Depending on the implementation, the memory may be allocated as a single chunk or as an array of pages.
		uint64 PageSize = 0;

		// Defines which immediate contexts are allowed to execute commands that use this device memory.

		// When ImmediateContextMask contains a bit at position n, the device memory may be
		// used in the immediate context with index n directly (see DeviceContextDesc::ContextId).
		// It may also be used in a command list recorded by a deferred context that will be executed
		// through that immediate context.
		//
		// \remarks    Only specify these bits that will indicate those immediate contexts where the device memory
		//             will actually be used. Do not set unnecessary bits as this will result in extra overhead.
		uint64 ImmediateContextMask = 1;

		constexpr DeviceMemoryDesc() noexcept {}

		constexpr DeviceMemoryDesc(
			DEVICE_MEMORY_TYPE _Type,
			uint32             _PageSize,
			uint64             _ImmediateContextMask = DeviceMemoryDesc{}.ImmediateContextMask) noexcept
			: Type(_Type)
			, PageSize(_PageSize)
			, ImmediateContextMask(_ImmediateContextMask)
		{
		}
	};

	// Device memory create information
	struct DeviceMemoryCreateInfo
	{
		// Device memory description, see shz::DeviceMemoryDesc.
		DeviceMemoryDesc  Desc;

		// Initial size of the memory object.

		// Some implementations do not support IDeviceMemory::Resize() and memory can only be allocated during the initialization.
		uint64 InitialSize = 0;

		// An array of `NumResources` resources that this memory must be compatible with.

		// For sparse memory, only shz::USAGE_SPARSE buffer and texture resources are allowed.
		// 
		// Vulkan backend requires at least one resource to be provided.
		//
		// In Direct3D12, the list of resources is optional on D3D12_RESOURCE_HEAP_TIER_2-hardware
		// and above, but is required on D3D12_RESOURCE_HEAP_TIER_1-hardware
		// (see SPARSE_RESOURCE_CAP_FLAG_MIXED_RESOURCE_TYPE_SUPPORT).
		//
		// It is recommended to always provide the list.
		IDeviceObject** ppCompatibleResources = nullptr;

		// The number of elements in the ppCompatibleResources array.
		uint32 NumResources = 0;
	};


	// Device memory interface

	// Defines the methods to manipulate a device memory object.
	struct SHZ_INTERFACE IDeviceMemory : public IDeviceObject
	{
		// Returns the device memory description
		virtual const DeviceMemoryDesc& GetDesc() const override = 0;

		// Resizes the internal memory object.

		// \param [in] NewSize - The new size of the memory object; must be a multiple of DeviceMemoryDesc::PageSize.
		//
		// Depending on the implementation, the function may resize the existing memory object or
		// create/destroy pages with separate memory objects.
		//
		// \remarks  This method must be externally synchronized with IDeviceMemory::GetCapacity()
		//           and IDeviceContext::BindSparseResourceMemory().
		virtual bool Resize(uint64 NewSize) = 0;

		// Returns the current size of the memory object.

		// \remarks  This method must be externally synchronized with IDeviceMemory::Resize()
		//           and IDeviceContext::BindSparseResourceMemory().
		virtual uint64 GetCapacity() const = 0;

		// Checks if the given resource is compatible with this memory object.
		virtual bool IsCompatible(IDeviceObject* pResource) const = 0;

		// AZ TODO:
		//virtual void DbgOnResourceDestroyed() = 0;
	};


} // namespace shz
