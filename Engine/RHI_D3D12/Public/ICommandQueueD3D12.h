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
 // Definition of the shz::ICommandQueueD3D12 interface

#include "Engine/RHI/Interface/ICommandQueue.h"

namespace shz
{

	// {D89693CE-F3F4-44B5-B7EF-24115AAD085E}
	static constexpr INTERFACE_ID IID_CommandQueueD3D12 =
	{ 0xd89693ce, 0xf3f4, 0x44b5, {0xb7, 0xef, 0x24, 0x11, 0x5a, 0xad, 0x8, 0x5e} };


	// This structure is used by ICommandQueueD3D12::UpdateTileMappings().
	struct ResourceTileMappingsD3D12
	{
		// A pointer to the reserved resource.
		ID3D12Resource* pResource = nullptr;

		// The number of reserved resource regions.
		UINT NumResourceRegions = 0;

		// An array of structures that describe the starting coordinates of the reserved resource regions.

		// The `NumResourceRegions` parameter specifies the number of elements in the array.
		const D3D12_TILED_RESOURCE_COORDINATE* pResourceRegionStartCoordinates = nullptr;

		// An array of structures that describe the sizes of the reserved resource regions. 

		// The `NumResourceRegions` parameter specifies the number of elements in the array.
		const D3D12_TILE_REGION_SIZE* pResourceRegionSizes = nullptr;

		// A pointer to the resource heap.
		ID3D12Heap* pHeap = nullptr;

		// The number of tile ranges.
		UINT NumRanges = 0;

		// A pointer to an array of `D3D12_TILE_RANGE_FLAGS` values that describes each tile range.

		// The NumRanges parameter specifies the number of values in the array.
		const D3D12_TILE_RANGE_FLAGS* pRangeFlags = nullptr;

		// An array of offsets into the resource heap. These are 0-based tile offsets, counting in tiles (not bytes).
		const UINT* pHeapRangeStartOffsets = nullptr;

		// An array of tiles. An array of values that specify the number of tiles in each tile range.
		// The NumRanges parameter specifies the number of values in the array.
		const UINT* pRangeTileCounts = nullptr;

		// A combination of D3D12_TILE_MAPPING_FLAGS values that are combined by using a bitwise OR operation.
		D3D12_TILE_MAPPING_FLAGS Flags = D3D12_TILE_MAPPING_FLAG_NONE;

		// Set to true if the resource has been created using NVApi.
		bool UseNVApi = false;
	};

	// Command queue interface
	struct SHZ_INTERFACE ICommandQueueD3D12 : public ICommandQueue
	{
		// Submits command lists for execution.

		// \param[in]  NumCommandLists - The number of command lists to submit.
		// \param[in]  ppCommandLists  - A pointer to the array of NumCommandLists command
		//                               lists to submit.
		//
		// \return Fence value associated with the executed command lists.
		virtual uint64 Submit(uint32 NumCommandLists, ID3D12CommandList* const* ppCommandLists) = 0;

		// Returns D3D12 command queue. May return null if queue is unavailable
		virtual ID3D12CommandQueue* GetD3D12CommandQueue() = 0;

		// Signals the given fence
		virtual void EnqueueSignal(ID3D12Fence* pFence, uint64       Value) = 0;

		// Instructs the GPU to wait until the fence reaches the specified value
		virtual void WaitFence(ID3D12Fence* pFence, uint64 Value) = 0;

		// Updates mappings of tile locations in reserved resources to memory locations in a resource heap.
		virtual void UpdateTileMappings(ResourceTileMappingsD3D12* pMappings, uint32 Count) = 0;

		// Returns the Direct3D12 command queue description
		virtual const D3D12_COMMAND_QUEUE_DESC& GetD3D12CommandQueueDesc() const = 0;
	};


} // namespace shz
