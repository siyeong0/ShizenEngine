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
 // Definition of the shz::IDeviceMemoryD3D12 interface

#include "Engine/RHI/Interface/IDeviceMemory.h"

namespace shz
{

	// {CC43FEB3-688F-4D4D-B493-0E509F4A0D02}
	static constexpr INTERFACE_ID IID_DeviceMemoryD3D12 =
	{ 0xcc43feb3, 0x688f, 0x4d4d, {0xb4, 0x93, 0xe, 0x50, 0x9f, 0x4a, 0xd, 0x2} };

	// This structure is returned by IDeviceMemoryD3D12::GetRange()
	struct DeviceMemoryRangeD3D12
	{
		// Pointer to the `ID3D12Heap` interface.
		ID3D12Heap* pHandle = nullptr;

		// Offset from the beginning of the heap to the start of the range, in bytes.
		uint64 Offset = 0;

		// Memory range size in bytes.

		// When IDeviceMemoryD3D12::GetRange() succeeds, the size is equal to the Size argument
		// that was given to the function, and zero otherwise.
		uint64 Size = 0;
	};

	// Exposes Direct3D12-specific functionality of a device memory object.
	struct SHZ_INTERFACE IDeviceMemoryD3D12 : public IDeviceMemory
	{
		// Returns a DeviceMemoryRangeD3D12 object with the information
		// about ID3D12Heap associated with the specified memory range.
		virtual DeviceMemoryRangeD3D12 GetRange(uint64 Offset, uint64 Size) const = 0;

		// Returns true if the heap was created using NVApi.
		virtual bool IsUsingNVApi() const = 0;
	};


} // namespace shz
