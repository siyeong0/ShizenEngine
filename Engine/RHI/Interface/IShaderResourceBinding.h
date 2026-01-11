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
 // Definition of the shz::IShaderResourceBinding interface and related data structures

#include "Primitives/Object.h"
#include "IShader.h"
#include "IShaderResourceVariable.h"
#include "IResourceMapping.h"

namespace shz
{

	struct IPipelineState;
	struct IPipelineResourceSignature;

	// {061F8774-9A09-48E8-8411-B5BD20560104}
	static constexpr INTERFACE_ID IID_ShaderResourceBinding =
	{ 0x61f8774, 0x9a09, 0x48e8, {0x84, 0x11, 0xb5, 0xbd, 0x20, 0x56, 0x1, 0x4} };

	// Shader resource binding interface
	struct SHZ_INTERFACE IShaderResourceBinding : public IObject
	{
		// Returns a pointer to the pipeline resource signature object that
		// defines the layout of this shader resource binding object.

		// The method does **NOT** increment the reference counter of the returned object,
		// so Release() **must not** be called.
		virtual struct IPipelineResourceSignature* GetPipelineResourceSignature() const = 0;


		// Binds SRB resources using the resource mapping

		// \param [in] ShaderStages - Flags that specify shader stages, for which resources will be bound.
		//                            Any combination of shz::SHADER_TYPE may be used.
		// \param [in] pResMapping  - Shader resource mapping where required resources will be looked up.
		// \param [in] Flags        - Additional flags. See shz::BIND_SHADER_RESOURCES_FLAGS.
		virtual void BindResources(
			SHADER_TYPE ShaderStages,
			IResourceMapping* pResMapping,
			BIND_SHADER_RESOURCES_FLAGS Flags) = 0;


		// Checks currently bound resources, see remarks.

		// \param [in] ShaderStages - Flags that specify shader stages, for which to check resources.
		//                            Any combination of shz::SHADER_TYPE may be used.
		// \param [in] pResMapping  - Optional shader resource mapping where resources will be looked up.
		//                            May be null.
		// \param [in] Flags        - Additional flags, see remarks.
		//
		// \return     Variable type flags that did not pass the checks and thus may need to be updated.
		//
		// This method may be used to perform various checks of the currently bound resources:
		//
		// - shz::BIND_SHADER_RESOURCES_UPDATE_MUTABLE and shz::BIND_SHADER_RESOURCES_UPDATE_DYNAMIC flags
		//   define which variable types to examine. Note that shz::BIND_SHADER_RESOURCES_UPDATE_STATIC
		//   has no effect as static resources are accessed through the PSO.
		//
		// - If shz::BIND_SHADER_RESOURCES_KEEP_EXISTING flag is not set and pResMapping is not null,
		//   the method will compare currently bound resources with the ones in the resource mapping.
		//   If any mismatch is found, the method will return the types of the variables that
		//   contain mismatching resources.
		//   Note that the situation when non-null object is bound to the variable, but the resource
		//   mapping does not contain an object corresponding to the variable name, does not count as
		//   mismatch.
		//
		// - If shz::BIND_SHADER_RESOURCES_VERIFY_ALL_RESOLVED flag is set, the method will check that
		//   all resources of the specified variable types are bound and return the types of the variables
		//   that are not bound.
		virtual SHADER_RESOURCE_VARIABLE_TYPE_FLAGS CheckResources(
			SHADER_TYPE ShaderStages,
			IResourceMapping* pResMapping,
			BIND_SHADER_RESOURCES_FLAGS Flags) const = 0;


		// Returns the variable by its name.

		// \param [in] ShaderType - Type of the shader to look up the variable.
		//                          Must be one of shz::SHADER_TYPE.
		// \param [in] Name       - Variable name.
		//
		// \note  This operation may potentially be expensive. If the variable will be used often, it is
		//        recommended to store and reuse the pointer as it never changes.
		virtual IShaderResourceVariable* GetVariableByName(SHADER_TYPE ShaderType, const Char* Name) = 0;


		// Returns the total variable count for the specific shader stage.

		// \param [in] ShaderType - Type of the shader.
		// \return Total number of variables in the shader stage.
		//
		// The method only counts mutable and dynamic variables that can be accessed through
		// the Shader Resource Binding object. Static variables are accessed through the Shader
		// object.
		virtual uint32 GetVariableCount(SHADER_TYPE ShaderType) const = 0;

		// Returns the variable by its index.

		// \param [in] ShaderType - Type of the shader to look up the variable.
		//                          Must be one of shz::SHADER_TYPE.
		// \param [in] Index      - Variable index. The index must be between 0 and the total number
		//                          of variables in this shader stage as returned by
		//                          IShaderResourceBinding::GetVariableCount().
		//
		// Only mutable and dynamic variables can be accessed through this method.
		// Static variables are accessed through the Shader object.
		//
		// \note   This operation may potentially be expensive. If the variable will be used often, it is
		//         recommended to store and reuse the pointer as it never changes.
		virtual IShaderResourceVariable* GetVariableByIndex(SHADER_TYPE ShaderType, uint32 Index) = 0;

		// Returns true if static resources have been initialized in this SRB.
		virtual bool StaticResourcesInitialized() const = 0;
	};


} // namespace shz
