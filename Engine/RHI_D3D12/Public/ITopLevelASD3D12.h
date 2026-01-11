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
 // Definition of the shz::ITopLevelASD3D12 interface

#include "Engine/RHI/Interface/ITopLevelAS.h"
#include "Engine/RHI/Interface/IDeviceContext.h"

namespace shz
{

	// {46334F12-64CB-4F7C-BB71-31515B6F386D}
	static constexpr INTERFACE_ID IID_TopLevelASD3D12 =
	{ 0x46334f12, 0x64cb, 0x4f7c, {0xbb, 0x71, 0x31, 0x51, 0x5b, 0x6f, 0x38, 0x6d} };


	// Exposes Direct3D12-specific functionality of a top-level acceleration structure object.
	struct SHZ_INTERFACE ITopLevelASD3D12 : public ITopLevelAS
	{
		// Returns `ID3D12Resource` interface of the internal D3D12 acceleration structure object.

		// The method does **NOT** increment the reference counter of the returned object,
		// so Release() **must not** be called.
		virtual ID3D12Resource* GetD3D12TLAS() = 0;

		// Returns a CPU descriptor handle of the D3D12 acceleration structure

		// The method does **NOT** increment the reference counter of the returned object,
		// so Release() **must not** be called.
		virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle() = 0;
	};


} // namespace shz
