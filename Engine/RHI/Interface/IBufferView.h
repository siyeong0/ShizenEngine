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
 // Definition of the shz::IBufferView interface and related data structures

#include "IDeviceObject.h"

namespace shz
{

	// {E2E83490-E9D2-495B-9A83-ABB413A38B07}
	static constexpr struct INTERFACE_ID IID_BufferView =
	{ 0xe2e83490, 0xe9d2, 0x495b, {0x9a, 0x83, 0xab, 0xb4, 0x13, 0xa3, 0x8b, 0x7} };

	// Buffer format description
	struct BufferFormat
	{
		// Type of components. For a formatted buffer views, this value cannot be VT_UNDEFINED
		VALUE_TYPE ValueType = VT_UNDEFINED;

		// Number of components. Allowed values: 1, 2, 3, 4.

		// For a formatted buffer, this value cannot be 0
		uint8 NumComponents = 0;

		// For signed and unsigned integer value types
		// (VT_INT8, VT_INT16, VT_INT32, VT_UINT8, VT_UINT16, VT_UINT32)
		// indicates if the value should be normalized to [-1,+1] or
		// [0, 1] range respectively. For floating point types
		// (VT_FLOAT16 and VT_FLOAT32), this member is ignored.
		bool IsNormalized = false;


		// We have to explicitly define constructors because otherwise Apple's clang fails to compile the following legitimate code:
		//     BufferFormat{VT_FLOAT32, 4}

		constexpr BufferFormat() noexcept {}

		constexpr BufferFormat(
			VALUE_TYPE _ValueType,
			uint8 _NumComponents,
			bool _IsNormalized = BufferFormat{}.IsNormalized) noexcept
			: ValueType(_ValueType)
			, NumComponents(_NumComponents)
			, IsNormalized(_IsNormalized)
		{
		}


		// Tests if two structures are equivalent
		constexpr bool operator == (const BufferFormat& rhs)const
		{
			return ValueType == rhs.ValueType &&
				NumComponents == rhs.NumComponents &&
				IsNormalized == rhs.IsNormalized;
		}
	};

	// Buffer view description
	struct BufferViewDesc : public DeviceObjectAttribs 
	{
		// View type. See shz::BUFFER_VIEW_TYPE for details.
		BUFFER_VIEW_TYPE ViewType = BUFFER_VIEW_UNDEFINED;

		// Format of the view. This member is only used for formatted and raw buffers.
		// To create raw view of a raw buffer, set Format.ValueType member to VT_UNDEFINED
		// (default value).
		struct BufferFormat Format;

		// Offset in bytes from the beginning of the buffer to the start of the
		// buffer region referenced by the view.
		uint64 ByteOffset = 0;

		// Size in bytes of the referenced buffer region.
		uint64 ByteWidth = 0;


		constexpr BufferViewDesc() noexcept {}

		explicit constexpr BufferViewDesc(
			const Char* _Name,
			BUFFER_VIEW_TYPE _ViewType,
			BufferFormat     _Format = BufferViewDesc{}.Format,
			uint64           _ByteOffset = BufferViewDesc{}.ByteOffset,
			uint64           _ByteWidth = BufferViewDesc{}.ByteWidth) noexcept
			: DeviceObjectAttribs(_Name)
			, ViewType(_ViewType)
			, Format(_Format)
			, ByteOffset(_ByteOffset)
			, ByteWidth(_ByteWidth)
		{
		}

		// Tests if two buffer view descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the buffer view properties.
		constexpr bool operator==(const BufferViewDesc& rhs) const
		{
			// Ignore name.
			return //strcmp(Name, rhs.Name) == 0 &&
				ViewType == rhs.ViewType &&
				ByteOffset == rhs.ByteOffset &&
				ByteWidth == rhs.ByteWidth &&
				Format == rhs.Format;
		}
	};


	// Buffer view interface

	// To create a buffer view, call IBuffer::CreateView().
	// \remarks
	// Buffer view holds strong references to the buffer. The buffer
	// will not be destroyed until all views are released.
	struct SHZ_INTERFACE IBufferView : public IDeviceObject
	{
		// Returns the buffer view description used to create the object
		virtual const BufferViewDesc& GetDesc() const override = 0;

		// Returns pointer to the referenced buffer object.

		// The method does **NOT** increment the reference counter of the returned object,
		// so Release() **must not** be called.
		virtual struct IBuffer* GetBuffer() const = 0;
	};


} // namespace shz
