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
 // Definition of the shz::IShaderResourceVariable interface and related data structures

#include "Primitives/BasicTypes.h"
#include "Primitives/Object.h"
#include "Primitives/FlagEnum.h"
#include "IDeviceObject.h"
#include "IShader.h"

namespace shz
{
	// {0D57DF3F-977D-4C8F-B64C-6675814BC80C}
	static constexpr INTERFACE_ID IID_ShaderResourceVariable =
	{ 0xd57df3f, 0x977d, 0x4c8f, {0xb6, 0x4c, 0x66, 0x75, 0x81, 0x4b, 0xc8, 0xc} };



	// Describes the type of the shader resource variable
	enum SHADER_RESOURCE_VARIABLE_TYPE : uint8
	{
		// Shader resource bound to the variable is the same for all SRB instances.
		// It must be set *once* directly through Pipeline State object.
		SHADER_RESOURCE_VARIABLE_TYPE_STATIC = 0,

		// Shader resource bound to the variable is specific to the shader resource binding
		// instance (see shz::IShaderResourceBinding). It must be set *once* through
		// shz::IShaderResourceBinding interface. It cannot be set through shz::IPipelineState
		// interface and cannot be change once bound.
		SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,

		// Shader variable binding is dynamic. It can be set multiple times for every instance of shader resource
		// binding (see shz::IShaderResourceBinding). It cannot be set through shz::IPipelineState interface.
		SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC,

		// Total number of shader variable types
		SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES
	};

	// Shader resource variable type flags
	enum SHADER_RESOURCE_VARIABLE_TYPE_FLAGS : uint32
	{
		// No flags
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_NONE = 0x00,

		// Static variable type flag
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_STATIC = (0x01 << SHADER_RESOURCE_VARIABLE_TYPE_STATIC),

		// Mutable variable type flag
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_MUTABLE = (0x01 << SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE),

		// Dynamic variable type flag
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_DYNAMIC = (0x01 << SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC),

		// Mutable and dynamic variable type flags
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_MUT_DYN =
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_MUTABLE |
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_DYNAMIC,

		// All variable type flags
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_ALL =
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_STATIC |
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_MUTABLE |
		SHADER_RESOURCE_VARIABLE_TYPE_FLAG_DYNAMIC
	};
	DEFINE_FLAG_ENUM_OPERATORS(SHADER_RESOURCE_VARIABLE_TYPE_FLAGS);


	// Shader resource binding flags
	enum BIND_SHADER_RESOURCES_FLAGS : uint32
	{
		// Indicates that static shader variable bindings are to be updated.
		BIND_SHADER_RESOURCES_UPDATE_STATIC = SHADER_RESOURCE_VARIABLE_TYPE_FLAG_STATIC,

		// Indicates that mutable shader variable bindings are to be updated.
		BIND_SHADER_RESOURCES_UPDATE_MUTABLE = SHADER_RESOURCE_VARIABLE_TYPE_FLAG_MUTABLE,

		// Indicates that dynamic shader variable bindings are to be updated.
		BIND_SHADER_RESOURCES_UPDATE_DYNAMIC = SHADER_RESOURCE_VARIABLE_TYPE_FLAG_DYNAMIC,

		// Indicates that all shader variable types (static, mutable and dynamic) are to be updated.
		// \note If none of BIND_SHADER_RESOURCES_UPDATE_STATIC, BIND_SHADER_RESOURCES_UPDATE_MUTABLE,
		//       and BIND_SHADER_RESOURCES_UPDATE_DYNAMIC flags are set, all variable types are updated
		//       as if BIND_SHADER_RESOURCES_UPDATE_ALL was specified.
		BIND_SHADER_RESOURCES_UPDATE_ALL = SHADER_RESOURCE_VARIABLE_TYPE_FLAG_ALL,

		// If this flag is specified, all existing bindings will be preserved and
		// only unresolved ones will be updated.
		// If this flag is not specified, every shader variable will be
		// updated if the mapping contains corresponding resource.
		BIND_SHADER_RESOURCES_KEEP_EXISTING = 0x08,

		// If this flag is specified, all shader bindings are expected
		// to be resolved after the call. If this is not the case, debug message
		// will be displayed.
		//
		// \note Only these variables are verified that are being updated by setting
		//       BIND_SHADER_RESOURCES_UPDATE_STATIC, BIND_SHADER_RESOURCES_UPDATE_MUTABLE, and
		//       BIND_SHADER_RESOURCES_UPDATE_DYNAMIC flags.
		BIND_SHADER_RESOURCES_VERIFY_ALL_RESOLVED = 0x10,

		// Allow overwriting static and mutable variables, see shz::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE.
		BIND_SHADER_RESOURCES_ALLOW_OVERWRITE = 0x20
	};
	DEFINE_FLAG_ENUM_OPERATORS(BIND_SHADER_RESOURCES_FLAGS);


	// Flags used by IShaderResourceVariable::Set, IShaderResourceVariable::SetArray,
	// and IShaderResourceVariable::SetBufferRange methods.
	enum SET_SHADER_RESOURCE_FLAGS : uint32
	{
		// No flags.
		SET_SHADER_RESOURCE_FLAG_NONE = 0,

		// Allow overwriting static and mutable variable bindings.
		//
		// By default, static and mutable variables can't be changed once
		// initialized to a non-null resource. This flag is required
		// to explicitly allow overwriting the binding.
		// 
		// Overwriting static variables does not require synchronization
		// with GPU and does not have effect on shader resource binding
		// objects already created from the pipeline state or resource signature. 
		// 
		// When overwriting a mutable variable binding in Direct3D12 and Vulkan,
		// an application must ensure that the GPU is not accessing the SRB.
		// This can be achieved using synchronization tools such as fences.
		// Synchronization with GPU is not required in OpenGL, Direct3D11,
		// and Metal backends.
		SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE = 1u << 0
	};
	DEFINE_FLAG_ENUM_OPERATORS(SET_SHADER_RESOURCE_FLAGS);


	// Shader resource variable
	struct SHZ_INTERFACE IShaderResourceVariable : public IObject
	{
		// Binds resource to the variable

		// The method performs run-time correctness checks.
		// For instance, shader resource view cannot
		// be assigned to a constant buffer variable.
		virtual void Set(IDeviceObject* pObject, SET_SHADER_RESOURCE_FLAGS Flags = SET_SHADER_RESOURCE_FLAG_NONE) = 0;

		// Binds resource array to the variable

		// \param [in] ppObjects    - a pointer to the array of objects.
		// \param [in] FirstElement - first array element to set.
		// \param [in] NumElements  - the number of objects in ppObjects array.
		// \param [in] Flags        - flags, see shz::SET_SHADER_RESOURCE_FLAGS.
		//
		// The method performs run-time correctness checks.
		// For instance, shader resource view cannot
		// be assigned to a constant buffer variable.
		virtual void SetArray(
			IDeviceObject* const* ppObjects,
			uint32 FirstElement,
			uint32 NumElements,
			SET_SHADER_RESOURCE_FLAGS Flags = SET_SHADER_RESOURCE_FLAG_NONE) = 0;

		// Binds the specified constant buffer range to the variable

		// \param [in] pObject    - pointer to the buffer object.
		// \param [in] Offset     - offset, in bytes, to the start of the buffer range to bind.
		// \param [in] Size       - size, in bytes, of the buffer range to bind.
		// \param [in] ArrayIndex - for array variables, index of the array element.
		// \param [in] Flags      - flags, see shz::SET_SHADER_RESOURCE_FLAGS.
		//
		// This method is only allowed for constant buffers. If dynamic offset is further set
		// by SetBufferOffset() method, it is added to the base offset set by this method.
		//
		// The method resets dynamic offset previously set for this variable to zero.
		//
		// \warning The Offset must be an integer multiple of ConstantBufferOffsetAlignment member
		//          specified by the device limits (see shz::DeviceLimits).
		virtual void SetBufferRange(
			IDeviceObject* pObject,
			uint64 Offset,
			uint64 Size,
			uint32 ArrayIndex = 0,
			SET_SHADER_RESOURCE_FLAGS Flags = SET_SHADER_RESOURCE_FLAG_NONE) = 0;


		// Sets the constant or structured buffer dynamic offset

		// \param [in] Offset     - additional offset, in bytes, that is added to the base offset (see remarks).
		//                          Only 32-bit offsets are supported.
		// \param [in] ArrayIndex - for array variables, index of the array element.
		//
		// This method is only allowed for constant or structured buffer variables that
		// were not created with shz::SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS or
		// shz::PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS flags. The method is also not
		// allowed for static resource variables.
		//
		// \note   The Offset must be an integer multiple of ConstantBufferOffsetAlignment member
		//         when setting the offset for a constant buffer, or StructuredBufferOffsetAlignment when
		//         setting the offset for a structured buffer, as specified by device limits
		//         (see shz::DeviceLimits).
		//
		// For constant buffers, the offset is added to the offset that was previously set
		// by SetBufferRange() method (if any). For structured buffers, the offset is added
		// to the base offset specified by the buffer view.
		//
		// Changing the buffer offset does not require committing the SRB.
		// From the engine point of view, buffers with dynamic offsets are treated similar to dynamic
		// buffers, and thus affected by shz::DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT flag.
		virtual void SetBufferOffset(uint32 Offset, uint32 ArrayIndex = 0) = 0;


		// Returns the shader resource variable type
		virtual SHADER_RESOURCE_VARIABLE_TYPE GetType() const = 0;


		// Returns shader resource description. See shz::ShaderResourceDesc.
		virtual void GetResourceDesc(ShaderResourceDesc& ResourceDesc) const = 0;


		// Returns the variable index that can be used to access the variable.
		virtual uint32 GetIndex() const = 0;


		// Returns a pointer to the resource that is bound to this variable.

		// \param [in] ArrayIndex - Resource array index. Must be 0 for
		//                          non-array variables.
		virtual IDeviceObject* Get(uint32 ArrayIndex = 0) const = 0;
	};


} // namespace shz
