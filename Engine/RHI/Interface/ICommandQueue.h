/*
 *  Copyright 2019-2023 Diligent Graphics LLC
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
 // Definition of the shz::ICommandQueue interface

#include "Primitives/Object.h"

namespace shz
{

	// {0FF427F7-6284-409E-8161-A023CA07EF5D}
	static constexpr INTERFACE_ID IID_CommandQueue =
	{ 0xff427f7, 0x6284, 0x409e, {0x81, 0x61, 0xa0, 0x23, 0xca, 0x7, 0xef, 0x5d} };

	// Command queue interface
	struct SHZ_INTERFACE ICommandQueue : public IObject
	{
		// Returns the value of the internal fence that will be signaled next time
		virtual uint64 GetNextFenceValue() const = 0;

		// Returns the last completed value of the internal fence
		virtual uint64 GetCompletedFenceValue() = 0;

		// Blocks execution until all pending GPU commands are complete
		virtual uint64 WaitForIdle() = 0;
	};



} // namespace shz
