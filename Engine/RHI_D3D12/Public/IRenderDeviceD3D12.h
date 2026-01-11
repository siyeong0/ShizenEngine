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
 // Definition of the shz::IRenderDeviceD3D12 interface

#include "Engine/RHI/Interface/IRenderDevice.h"

namespace shz
{

	// {C7987C98-87FE-4309-AE88-E98F044B00F6}
	static constexpr INTERFACE_ID IID_RenderDeviceD3D12 =
	{ 0xc7987c98, 0x87fe, 0x4309, {0xae, 0x88, 0xe9, 0x8f, 0x4, 0x4b, 0x0, 0xf6} };

	// Exposes Direct3D12-specific functionality of a render device.
	struct SHZ_INTERFACE IRenderDeviceD3D12 : public IRenderDevice
	{
		// Returns `ID3D12Device` interface of the internal Direct3D12 device object.

		// The method does **NOT** increment the reference counter of the returned object,
		// so Release() **must not** be called.
		virtual ID3D12Device* GetD3D12Device() const = 0;

		// Creates a texture object from native d3d12 resource

		// \param [in]  pd3d12Texture - pointer to the native D3D12 texture
		// \param [in]  InitialState  - Initial texture state. See shz::RESOURCE_STATE.
		// \param [out] ppTexture     - Address of the memory location where the pointer to the
		//                              texture interface will be stored.
		//                              The function calls AddRef(), so that the new object will contain
		//                              one reference.
		virtual void CreateTextureFromD3DResource(
			ID3D12Resource* pd3d12Texture,
			RESOURCE_STATE  InitialState,
			ITexture** ppTexture) = 0;

		// Creates a buffer object from native d3d12 resource

		// \param [in]  pd3d12Buffer - Pointer to the native d3d12 buffer resource
		// \param [in]  BuffDesc     - Buffer description. The system can recover buffer size, but
		//                             the rest of the fields need to be populated by the client
		//                             as they cannot be recovered from d3d12 resource description
		// \param [in]  InitialState - Initial buffer state. See shz::RESOURCE_STATE.
		// \param [out] ppBuffer     - Address of the memory location where the pointer to the
		//                             buffer interface will be stored.
		//                             The function calls AddRef(), so that the new object will contain
		//                             one reference.
		virtual void CreateBufferFromD3DResource(
			ID3D12Resource* pd3d12Buffer,
			const BufferDesc& BuffDesc,
			RESOURCE_STATE InitialState,
			IBuffer** ppBuffer) = 0;

		// Creates a bottom-level AS object from native d3d12 resource

		// \param [in]  pd3d12BLAS   - Pointer to the native d3d12 acceleration structure resource
		// \param [in]  Desc         - Bottom-level AS description.
		// \param [in]  InitialState - Initial BLAS state. Can be shz::RESOURCE_STATE_UNKNOWN,
		//                             shz::RESOURCE_STATE_BUILD_AS_READ, shz::RESOURCE_STATE_BUILD_AS_WRITE.
		//                             See shz::RESOURCE_STATE.
		// \param [out] ppBLAS       - Address of the memory location where the pointer to the
		//                             bottom-level AS interface will be stored.
		//                             The function calls AddRef(), so that the new object will contain
		//                             one reference.
		virtual void CreateBLASFromD3DResource(
			ID3D12Resource* pd3d12BLAS,
			const BottomLevelASDesc& Desc,
			RESOURCE_STATE InitialState,
			IBottomLevelAS** ppBLAS) = 0;

		// Creates a top-level AS object from native d3d12 resource

		// \param [in]  pd3d12TLAS   - Pointer to the native d3d12 acceleration structure resource
		// \param [in]  Desc         - Top-level AS description.
		// \param [in]  InitialState - Initial TLAS state. Can be shz::RESOURCE_STATE_UNKNOWN,
		//                             shz::RESOURCE_STATE_BUILD_AS_READ, shz::RESOURCE_STATE_BUILD_AS_WRITE,
		//                             shz::RESOURCE_STATE_RAY_TRACING. See shz::RESOURCE_STATE.
		// \param [out] ppTLAS       - Address of the memory location where the pointer to the
		//                             top-level AS interface will be stored.
		//                             The function calls AddRef(), so that the new object will contain
		//                             one reference.
		virtual void CreateTLASFromD3DResource(
			ID3D12Resource* pd3d12TLAS,
			const TopLevelASDesc& Desc,
			RESOURCE_STATE InitialState,
			ITopLevelAS** ppTLAS) = 0;

		// Returns DX compiler interface, or null if the compiler is not loaded.
		virtual struct IDXCompiler* GetDXCompiler() const = 0;
	};


} // namespace shz
