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
 // Defines basic types used in the engine

#include "CommonDefinitions.h"
#include "Primitives/Common.h"
#include <string>

namespace shz
{

	using float32 = float;	///< 32-bit float
	using float64 = double;	///< 64-bit float

	using int64 = int64_t;	///< 64-bit signed integer
	using int32 = int32_t;	///< 32-bit signed integer
	using int16 = int16_t;	///< 16-bit signed integer
	using int8 = int8_t;	///< 8-bit signed integer

	using uint = unsigned int;	///< Unsigned integer (platform-dependent width)
	using uint64 = uint64_t;	///< 64-bit unsigned integer
	using uint32 = uint32_t;	///< 32-bit unsigned integer
	using uint16 = uint16_t;	///< 16-bit unsigned integer
	using uint8 = uint8_t;		///< 8-bit unsigned integer

	using byte = unsigned char; ///< 8-bit byte

	using Char = char; ///< Character type
	using String = std::basic_string<Char>; ///< String variable

	struct int2
	{
		int x;
		int y;

		int operator[](size_t idx) const { return (&x)[idx]; }
	};

	struct int3
	{
		int x;
		int y;
		int z;

		int operator[](size_t idx) const { return (&x)[idx]; }
	};

	struct uint2
	{
		uint x;
		uint y;

		uint operator[](size_t idx) const { return (&x)[idx]; }
	};

	struct uint3
	{
		uint x;
		uint y;
		uint z;

		uint operator[](size_t idx) const { return (&x)[idx]; }
	};

	struct uint4
	{
		uint x;
		uint y;
		uint z;
		uint w;

		uint operator[](size_t idx) const { return (&x)[idx]; }
	};

} // namespace shz
