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
 // Definition of the shz::IPipelineStateCache interface and related data structures

#include "Primitives/Object.h"
#include "Primitives/DataBlob.h"
#include "GraphicsTypes.h"
#include "IDeviceObject.h"

namespace shz
{
	// Pipeline state cache mode.
	enum PSO_CACHE_MODE : uint8
	{
		// PSO cache will be used to load PSOs from it.
		PSO_CACHE_MODE_LOAD = 1u << 0u,

		// PSO cache will be used to store PSOs.
		PSO_CACHE_MODE_STORE = 1u << 1u,

		// PSO cache will be used to load and store PSOs.
		PSO_CACHE_MODE_LOAD_STORE = PSO_CACHE_MODE_LOAD | PSO_CACHE_MODE_STORE
	};
	DEFINE_FLAG_ENUM_OPERATORS(PSO_CACHE_MODE);

	// Pipeline state cache flags.
	enum PSO_CACHE_FLAGS : uint8
	{
		// No flags.
		PSO_CACHE_FLAG_NONE = 0u,

		// Print diagnostic messages e.g. when PSO is not found in the cache.
		PSO_CACHE_FLAG_VERBOSE = 1u << 0u
	};
	DEFINE_FLAG_ENUM_OPERATORS(PSO_CACHE_FLAGS);

	// Pipeline state cache description
	struct PipelineStateCacheDesc : public DeviceObjectAttribs 
	{

		// Cache mode, see shz::PSO_CACHE_MODE.

		// Metal backend allows generating the cache on one device
		// and loading PSOs from it on another.
		//
		// Vulkan PSO cache depends on the GPU device, driver version and other parameters,
		// so the cache must be generated and used on the same device.
		PSO_CACHE_MODE Mode = PSO_CACHE_MODE_LOAD_STORE;

		// PSO cache flags, see shz::PSO_CACHE_FLAGS.
		PSO_CACHE_FLAGS Flags = PSO_CACHE_FLAG_NONE;

		// ImmediateContextMask ?
	};

	// Pipeline state pbject cache create info
	struct PipelineStateCacheCreateInfo
	{
		// Pipeline state cache description
		PipelineStateCacheDesc Desc;

		// All fields can be null to create an empty cache
		const void* pCacheData = nullptr;

		// The size of data pointed to by pCacheData
		uint32      CacheDataSize = 0;
	};

	// {6AC86F22-FFF4-493C-8C1F-C539D934F4BC}
	static constexpr INTERFACE_ID IID_PipelineStateCache =
	{ 0x6ac86f22, 0xfff4, 0x493c, {0x8c, 0x1f, 0xc5, 0x39, 0xd9, 0x34, 0xf4, 0xbc} };

	// Pipeline state cache interface
	struct SHZ_INTERFACE IPipelineStateCache : public IDeviceObject
	{
		// Creates a blob with pipeline state cache data
		virtual void GetData(IDataBlob** ppBlob) = 0;
	};


}
