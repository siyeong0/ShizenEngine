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
 // Definition of the shz::IShader interface and related data structures

#include "Primitives/FileStream.h"
#include "Primitives/FlagEnum.h"
#include "IDeviceObject.h"

namespace shz
{

	// {2989B45C-143D-4886-B89C-C3271C2DCC5D}
	static constexpr INTERFACE_ID IID_Shader =
	{ 0x2989b45c, 0x143d, 0x4886, {0xb8, 0x9c, 0xc3, 0x27, 0x1c, 0x2d, 0xcc, 0x5d} };


	using ShaderVersion = Version;

	// Describes the shader source code language
	enum SHADER_SOURCE_LANGUAGE : uint32
	{
		// Default language (GLSL for OpenGL/OpenGLES/Vulkan devices, HLSL for Direct3D11/Direct3D12 devices)
		SHADER_SOURCE_LANGUAGE_DEFAULT = 0,

		// The source language is HLSL
		SHADER_SOURCE_LANGUAGE_HLSL,

		// The source language is GLSL
		SHADER_SOURCE_LANGUAGE_GLSL,

		// The source language is GLSL that should be compiled verbatim

		// By default the engine prepends GLSL shader source code with platform-specific
		// definitions. For instance it adds appropriate #version directive (e.g. `#version 430 core` or
		// `#version 310 es`) so that the same source will work on different versions of desktop OpenGL and OpenGLES.
		// When `SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM` is used, the source code will be compiled as is.
		// Note that shader macros are ignored when compiling GLSL verbatim in OpenGL backend, and an application
		// should add the macro definitions to the source code.
		SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM,

		// The source language is Metal shading language (MSL)
		SHADER_SOURCE_LANGUAGE_MSL,

		// The source language is Metal shading language (MSL) that should be compiled verbatim

		// Note that shader macros are ignored when compiling MSL verbatim, and an application
		// should add the macro definitions to the source code.
		SHADER_SOURCE_LANGUAGE_MSL_VERBATIM,

		// The source language is Metal bytecode
		SHADER_SOURCE_LANGUAGE_MTLB,

		// The source language is WebGPU shading language (WGSL)
		SHADER_SOURCE_LANGUAGE_WGSL,

		// The shader source is provided as device-specific bytecode
		// (e.g. DXBC or DXIL for Direct3D11/Direct3D12, SPIRV for Vulkan, etc.).
		// The bytecode is used verbatim and no compilation is performed.
		// 
		// This option is similar to providing the byte code via `ShaderCreateInfo::ByteCode`.
		SHADER_SOURCE_LANGUAGE_BYTECODE,

		SHADER_SOURCE_LANGUAGE_COUNT
	};


	// Describes the shader compiler that will be used to compile the shader source code
	enum SHADER_COMPILER : uint32
	{
		// Default compiler for specific language and API that is selected as follows:
		//
		//     - Direct3D11:      legacy HLSL compiler (FXC)
		//     - Direct3D12:      legacy HLSL compiler (FXC)
		//     - OpenGL(ES) GLSL: native compiler
		//     - OpenGL(ES) HLSL: HLSL2GLSL converter and native compiler
		//     - Vulkan GLSL:     built-in glslang
		//     - Vulkan HLSL:     built-in glslang (with limited support for Shader Model 6.x)
		//     - Metal GLSL/HLSL: built-in glslang (HLSL with limited support for Shader Model 6.x)
		//     - Metal MSL:       native compiler
		SHADER_COMPILER_DEFAULT = 0,

		// Built-in glslang compiler for GLSL and HLSL.
		SHADER_COMPILER_GLSLANG,

		// Modern HLSL compiler (DXC) for Direct3D12 and Vulkan with Shader Model 6.x support.
		SHADER_COMPILER_DXC,

		// Legacy HLSL compiler (FXC) for Direct3D11 and Direct3D12 supporting shader models up to 5.1.
		SHADER_COMPILER_FXC,

		SHADER_COMPILER_LAST = SHADER_COMPILER_FXC,
		SHADER_COMPILER_COUNT
	};


	// Describes the flags that can be passed over to IShaderSourceInputStreamFactory::CreateInputStream2() function.
	enum CREATE_SHADER_SOURCE_INPUT_STREAM_FLAGS : uint32
	{
		// No flag.
		CREATE_SHADER_SOURCE_INPUT_STREAM_FLAG_NONE = 0x00,

		// Do not output any messages if the file is not found or
		// other errors occur.
		CREATE_SHADER_SOURCE_INPUT_STREAM_FLAG_SILENT = 0x01,
	};
	DEFINE_FLAG_ENUM_OPERATORS(CREATE_SHADER_SOURCE_INPUT_STREAM_FLAGS);

	// Shader description
	struct ShaderDesc : public DeviceObjectAttribs 
	{
		// Shader type. See shz::SHADER_TYPE.
		SHADER_TYPE ShaderType = SHADER_TYPE_UNKNOWN;

		// Whether to use combined texture samplers.

		// If set to `true`, textures will be combined with texture samplers.
		// The `CombinedSamplerSuffix` member defines the suffix added to the texture
		// variable name to get corresponding sampler name. When using combined samplers,
		// the sampler assigned to the shader resource view is automatically set when
		// the view is bound. Otherwise, samplers need to be explicitly set similar to other
		// shader variables.
		//
		// This member has no effect if the shader is used in the PSO that uses pipeline resource signature(s).
		bool UseCombinedTextureSamplers = false;

		// Combined sampler suffix.

		// If `UseCombinedTextureSamplers` is `true`, defines the suffix added to the
		// texture variable name to get corresponding sampler name.  For example,
		// for default value `"_sampler"`, a texture named `"tex"` will be combined
		// with the sampler named `"tex_sampler"`.
		// If `UseCombinedTextureSamplers` is `false`, this member is ignored.
		//
		// This member has no effect if the shader is used in the PSO that uses pipeline resource signature(s).
		const Char* CombinedSamplerSuffix = "_sampler";

		constexpr ShaderDesc() noexcept {}

		constexpr ShaderDesc(
			const Char* _Name,
			SHADER_TYPE _ShaderType,
			bool        _UseCombinedTextureSamplers = ShaderDesc{}.UseCombinedTextureSamplers,
			const Char* _CombinedSamplerSuffix = ShaderDesc{}.CombinedSamplerSuffix)
			: DeviceObjectAttribs(_Name)
			, ShaderType(_ShaderType)
			, UseCombinedTextureSamplers(_UseCombinedTextureSamplers)
			, CombinedSamplerSuffix(_CombinedSamplerSuffix)
		{
		}

		// Tests if two shader descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the shader properties.
		bool operator==(const ShaderDesc& rhs) const noexcept
		{
			// Ignore Name. This is consistent with the hasher (HashCombiner<HasherType, ShaderDesc>).
			return ShaderType == rhs.ShaderType &&
				UseCombinedTextureSamplers == rhs.UseCombinedTextureSamplers &&
				SafeStrEqual(CombinedSamplerSuffix, rhs.CombinedSamplerSuffix);
		}
		bool operator!=(const ShaderDesc& rhs) const noexcept
		{
			return !(*this == rhs);
		}

	};


	// Shader status
	enum SHADER_STATUS : uint32
	{
		// Initial shader status.
		SHADER_STATUS_UNINITIALIZED = 0,

		// The shader is being compiled.
		SHADER_STATUS_COMPILING,

		// The shader has been successfully compiled
		// and is ready to be used.
		SHADER_STATUS_READY,

		// The shader compilation has failed.
		SHADER_STATUS_FAILED
	};



	// {3EA98781-082F-4413-8C30-B9BA6D82DBB7}
	static constexpr INTERFACE_ID IID_IShaderSourceInputStreamFactory =
	{ 0x3ea98781, 0x82f, 0x4413, {0x8c, 0x30, 0xb9, 0xba, 0x6d, 0x82, 0xdb, 0xb7} };


	// Shader source stream factory interface
	struct SHZ_INTERFACE IShaderSourceInputStreamFactory : public IObject
	{
		// Creates a shader source input stream for the specified file name.

		// The stream is used to load the shader source code.
		// \param [in] Name      - The name of the file to load.
		// \param [out] ppStream - Pointer to the shader source input stream.
		virtual void CreateInputStream(const Char* Name, IFileStream** ppStream) = 0;

		// Creates a shader source input stream for the specified file name.

		// The stream is used to load the shader source code.
		// \param [in] Name      - The name of the file to load.
		// \param [in] Flags     - Flags that control the stream creation,
		//                         see shz::CREATE_SHADER_SOURCE_INPUT_STREAM_FLAGS.
		// \param [out] ppStream - Pointer to the shader source input stream.
		virtual void CreateInputStream2(const Char* Name, CREATE_SHADER_SOURCE_INPUT_STREAM_FLAGS Flags, IFileStream** ppStream) = 0;
	};


	// Shader Macro
	struct ShaderMacro
	{
		// Macro name
		const Char* Name = nullptr;

		// Macro definition
		const Char* Definition = nullptr;

		constexpr ShaderMacro() noexcept
		{
		}

		constexpr ShaderMacro(const Char* _Name, const Char* _Def) noexcept
			: Name(_Name)
			, Definition(_Def)
		{
		}

		// Comparison operator tests if two structures are equivalent
		bool operator==(const ShaderMacro& rhs) const noexcept
		{
			return SafeStrEqual(Name, rhs.Name) && SafeStrEqual(Definition, rhs.Definition);
		}
		bool operator!=(const ShaderMacro& rhs) const noexcept
		{
			return !(*this == rhs);
		}
	};


	// Shader macro array
	struct ShaderMacroArray
	{
		// A pointer to the array elements
		const ShaderMacro* Elements = nullptr;

		// The number of elements in the array
		uint32 Count = 0;

		constexpr ShaderMacroArray() noexcept
		{
		}

		constexpr ShaderMacroArray(const ShaderMacro* _Elements, uint32 _Count) noexcept
			: Elements(_Elements)
			, Count(_Count)
		{
		}

		constexpr bool operator==(const ShaderMacroArray& rhs) const noexcept
		{
			if (Count != rhs.Count)
				return false;

			if ((Count != 0 && Elements == nullptr) || (rhs.Count != 0 && rhs.Elements == nullptr))
				return false;
			for (uint32 i = 0; i < Count; ++i)
			{
				if (Elements[i] != rhs.Elements[i])
					return false;
			}
			return true;
		}

		constexpr bool operator!=(const ShaderMacroArray& rhs) const noexcept
		{
			return !(*this == rhs);
		}

		explicit constexpr operator bool() const noexcept
		{
			return Elements != nullptr && Count > 0;
		}

		const ShaderMacro& operator[](size_t index) const noexcept
		{
			return Elements[index];
		}
	};


	// Shader compilation flags
	enum SHADER_COMPILE_FLAGS : uint32
	{
		// No flags.
		SHADER_COMPILE_FLAG_NONE = 0,

		// Enable unbounded resource arrays (e.g. `Texture2D g_Texture[]`).
		SHADER_COMPILE_FLAG_ENABLE_UNBOUNDED_ARRAYS = 1u << 0u,

		// Don't load shader reflection.
		SHADER_COMPILE_FLAG_SKIP_REFLECTION = 1u << 1u,

		// Compile the shader asynchronously.
		SHADER_COMPILE_FLAG_ASYNCHRONOUS = 1u << 2u,

		// Pack matrices in row-major order.
		SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR = 1u << 3u,

		// Convert HLSL to GLSL when compiling HLSL shaders to SPIRV.
		SHADER_COMPILE_FLAG_HLSL_TO_SPIRV_VIA_GLSL = 1u << 4u,

		// Disable shader optimization.
		//
		// FXC  : adds D3DCOMPILE_SKIP_OPTIMIZATION
		// DXC  : adds -Od
		//
		// This flag is intended for debugging only.
		SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION = 1u << 5u,

		SHADER_COMPILE_FLAG_LAST = SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION
	};
	DEFINE_FLAG_ENUM_OPERATORS(SHADER_COMPILE_FLAGS);


	// Shader creation attributes
	struct ShaderCreateInfo
	{
		// Source file path

		// If source file path is provided, `Source` and `ByteCode` members must be null
		const Char* FilePath = nullptr;

		// Pointer to the shader source input stream factory.

		// The factory is used to load the shader source file if FilePath is not null.
		// It is also used to create additional input streams for shader include files
		IShaderSourceInputStreamFactory* pShaderSourceStreamFactory = nullptr;

		// Shader source

		// If shader source is provided, FilePath and ByteCode members must be null
		const Char* Source = nullptr;

		// Compiled shader bytecode.

		// If shader byte code is provided, FilePath and Source members must be null
		//
		// This option is supported for D3D11, D3D12, Vulkan and Metal backends.
		// For D3D11 and D3D12 backends, DXBC should be provided.
		// Vulkan backend expects SPIRV bytecode.
		// Metal backend supports .metallib bytecode to create MTLLibrary
		// or SPIRV to translate it to MSL and compile (may be slow).
		//
		// If SHADER_COMPILE_FLAG_SKIP_REFLECTION flag is not used, the bytecode
		// must contain reflection information. If shaders were compiled
		// using fxc, make sure that `/Qstrip_reflect` option is **not** specified.
		// HLSL shaders need to be compiled against 4.0 profile or higher.
		const void* ByteCode = nullptr;

		union
		{
			// Length of the source code, when `Source` is not null.

			// When Source is not null and is not a null-terminated string, this member
			// should be used to specify the length of the source code.
			// If `SourceLength` is zero, the source code string is assumed to be
			// null-terminated.
			size_t SourceLength = 0;


			// Size of the compiled shader byte code, when `ByteCode` is not null.

			// Byte code size (in bytes) must not be zero if `ByteCode` is not null.
			size_t ByteCodeSize;
		};

		// Shader entry point

		// This member is ignored if ByteCode is not null
		const Char* EntryPoint = "main";

		// Shader macros (see shz::ShaderMacroArray)
		ShaderMacroArray Macros;

		// Shader description. See shz::ShaderDesc.
		ShaderDesc Desc;

		// Shader source language. See shz::SHADER_SOURCE_LANGUAGE.
		SHADER_SOURCE_LANGUAGE SourceLanguage = SHADER_SOURCE_LANGUAGE_DEFAULT;

		// Shader compiler. See shz::SHADER_COMPILER.
		SHADER_COMPILER ShaderCompiler = SHADER_COMPILER_DEFAULT;

		// HLSL shader model to use when compiling the shader.

		// When default value is given (0, 0), the engine will attempt to use the highest HLSL shader model
		// supported by the device. If the shader is created from the byte code, this value
		// has no effect.
		//
		// \note When HLSL source is converted to GLSL, corresponding GLSL/GLESSL version will be used.
		ShaderVersion HLSLVersion = {};

		// GLSL version to use when creating the shader.

		// When default value is given (0, 0), the engine will attempt to use the highest GLSL version
		// supported by the device.
		ShaderVersion GLSLVersion = {};

		// GLES shading language version to use when creating the shader.

		// When default value is given (0, 0), the engine will attempt to use the highest GLESSL version
		// supported by the device.
		ShaderVersion GLESSLVersion = {};

		// Metal shading language version to use when creating the shader.

		// When default value is given (0, 0), the engine will attempt to use the highest MSL version
		// supported by the device.
		ShaderVersion MSLVersion = {};

		// Shader compile flags (see shz::SHADER_COMPILE_FLAGS).
		SHADER_COMPILE_FLAGS CompileFlags = SHADER_COMPILE_FLAG_NONE;

		// Whether to load constant buffer reflection information.

		// The reflection information can be queried through
		// IShader::GetConstantBufferDesc() method.

		// \note Loading constant buffer reflection introduces some overhead,
		//       and should be disabled when it is not needed.
		bool LoadConstantBufferReflection = false;

		// An optional list of GLSL extensions to enable when compiling GLSL source code.
		const char* GLSLExtensions = nullptr;

		// Emulated array index suffix for WebGPU backend.

		// An optional suffix to append to the name of emulated array variables to get
		// the indexed array element name.
		//
		// Since WebGPU does not support arrays of resources, Diligent Engine
		// emulates them by appending an index to the resource name.
		// For instance, if the suffix is set to `"_"`, resources named
		// `"g_Tex2D_0"`, `"g_Tex2D_1"`, `"g_Tex2D_2"` will be grouped into an array
		// of 3 textures named `"g_Tex2D"`. All resources must be the same type
		// to be grouped into an array.
		//
		// When suffix is null or empty, no array emulation is performed.
		//
		// \remarks    This member is ignored when compiling shaders for backends other than WebGPU.
		const char* WebGPUEmulatedArrayIndexSuffix = nullptr;

		constexpr ShaderCreateInfo() noexcept
		{
		}

		constexpr ShaderCreateInfo(
			const Char* _FilePath,
			IShaderSourceInputStreamFactory* _pSourceFactory,
			SHADER_SOURCE_LANGUAGE _SourceLanguage = ShaderCreateInfo{}.SourceLanguage,
			const ShaderDesc& _Desc = ShaderDesc{}) noexcept
			: FilePath(_FilePath)
			, pShaderSourceStreamFactory(_pSourceFactory)
			, Desc(_Desc)
			, SourceLanguage(_SourceLanguage)
		{
		}

		constexpr ShaderCreateInfo(
			const Char* _FilePath,
			IShaderSourceInputStreamFactory* _pSourceFactory,
			const Char* _EntryPoint,
			const ShaderMacroArray& _Macros = ShaderCreateInfo{}.Macros,
			SHADER_SOURCE_LANGUAGE _SourceLanguage = ShaderCreateInfo{}.SourceLanguage,
			const ShaderDesc& _Desc = ShaderDesc{}) noexcept
			: FilePath(_FilePath)
			, pShaderSourceStreamFactory(_pSourceFactory)
			, EntryPoint(_EntryPoint)
			, Macros(_Macros)
			, Desc(_Desc)
			, SourceLanguage(_SourceLanguage)
		{
		}

		constexpr ShaderCreateInfo(
			const Char* _Source,
			size_t _SourceLength,
			const Char* _EntryPoint = ShaderCreateInfo{}.EntryPoint,
			const ShaderMacroArray& _Macros = ShaderCreateInfo{}.Macros,
			SHADER_SOURCE_LANGUAGE  _SourceLanguage = ShaderCreateInfo{}.SourceLanguage,
			const ShaderDesc& _Desc = ShaderDesc{}) noexcept
			: Source(_Source)
			, SourceLength(_SourceLength)
			, EntryPoint(_EntryPoint)
			, Macros(_Macros)
			, Desc(_Desc)
			, SourceLanguage(_SourceLanguage)
		{
		}

		constexpr ShaderCreateInfo(
			const Char* _Source,
			size_t _SourceLength,
			const Char* _EntryPoint = ShaderCreateInfo{}.EntryPoint,
			SHADER_SOURCE_LANGUAGE _SourceLanguage = ShaderCreateInfo{}.SourceLanguage,
			const ShaderDesc& _Desc = ShaderDesc{}) noexcept
			: Source(_Source)
			, SourceLength(_SourceLength)
			, EntryPoint(_EntryPoint)
			, Desc(_Desc)
			, SourceLanguage(_SourceLanguage)
		{
		}

		constexpr ShaderCreateInfo(const void* _ByteCode, size_t _ByteCodeSize) noexcept
			: ByteCode(_ByteCode)
			, ByteCodeSize(_ByteCodeSize)
		{
		}

		// Comparison operator tests if two structures are equivalent.
		//
		// \note   Comparison ignores shader name.
		bool operator==(const ShaderCreateInfo& rhs) const noexcept
		{
			const auto& CI1 = *this;
			const auto& CI2 = rhs;

			if (!SafeStrEqual(CI1.FilePath, CI2.FilePath))
				return false;

			//if (CI1.FilePath != nullptr && CI1.pShaderSourceStreamFactory != CI2.pShaderSourceStreamFactory)
			//    return false;

			if (!SafeStrEqual(CI1.Source, CI2.Source))
				return false;

			if (CI1.SourceLength != CI2.SourceLength)
				return false;

			if ((CI1.ByteCode != nullptr) != (CI2.ByteCode != nullptr))
				return false;

			if ((CI1.ByteCode != nullptr) && (CI2.ByteCode != nullptr))
			{
				if (memcmp(CI1.ByteCode, CI2.ByteCode, CI1.ByteCodeSize) != 0)
					return false;
			}

			if (!SafeStrEqual(CI1.EntryPoint, CI2.EntryPoint))
				return false;

			if (CI1.Macros != CI2.Macros)
				return false;

			if (CI1.Desc != CI2.Desc)
				return false;

			if (CI1.SourceLanguage != CI2.SourceLanguage)
				return false;

			if (CI1.ShaderCompiler != CI2.ShaderCompiler)
				return false;

			if ((CI1.HLSLVersion != CI2.HLSLVersion ||
				CI1.GLSLVersion != CI2.GLSLVersion ||
				CI1.GLESSLVersion != CI2.GLESSLVersion ||
				CI1.MSLVersion != CI2.MSLVersion))
				return false;

			if (CI1.CompileFlags != CI2.CompileFlags)
				return false;

			if (CI1.LoadConstantBufferReflection != CI2.LoadConstantBufferReflection)
				return false;

			if (!SafeStrEqual(CI1.GLSLExtensions, CI2.GLSLExtensions))
				return false;

			if (!SafeStrEqual(CI1.WebGPUEmulatedArrayIndexSuffix, CI2.WebGPUEmulatedArrayIndexSuffix))
				return false;

			return true;
		}

		bool operator!=(const ShaderCreateInfo& rhs) const noexcept
		{
			return !(*this == rhs);
		}
	};


	// Describes shader resource type
	enum SHADER_RESOURCE_TYPE : uint8
	{
		// Shader resource type is unknown
		SHADER_RESOURCE_TYPE_UNKNOWN = 0,

		// Constant (uniform) buffer
		SHADER_RESOURCE_TYPE_CONSTANT_BUFFER,

		// Shader resource view of a texture (sampled image)
		SHADER_RESOURCE_TYPE_TEXTURE_SRV,

		// Shader resource view of a buffer (read-only storage image)
		SHADER_RESOURCE_TYPE_BUFFER_SRV,

		// Unordered access view of a texture (storage image)
		SHADER_RESOURCE_TYPE_TEXTURE_UAV,

		// Unordered access view of a buffer (storage buffer)
		SHADER_RESOURCE_TYPE_BUFFER_UAV,

		// Sampler (separate sampler)
		SHADER_RESOURCE_TYPE_SAMPLER,

		// Input attachment in a render pass
		SHADER_RESOURCE_TYPE_INPUT_ATTACHMENT,

		// Acceleration structure
		SHADER_RESOURCE_TYPE_ACCEL_STRUCT,

		SHADER_RESOURCE_TYPE_LAST = SHADER_RESOURCE_TYPE_ACCEL_STRUCT
	};


	// Shader resource description
	struct ShaderResourceDesc
	{
		// Shader resource name
		const Char* Name = nullptr;

		// Shader resource type, see shz::SHADER_RESOURCE_TYPE.
		SHADER_RESOURCE_TYPE Type = SHADER_RESOURCE_TYPE_UNKNOWN;

		// Array size. For non-array resource this value is 1.
		uint32 ArraySize = 0;

		constexpr ShaderResourceDesc() noexcept
		{
		}

		constexpr ShaderResourceDesc(
			const Char* _Name,
			SHADER_RESOURCE_TYPE _Type,
			uint32               _ArraySize) noexcept
			: Name(_Name)
			, Type(_Type)
			, ArraySize(_ArraySize)
		{
		}

		// Comparison operator tests if two structures are equivalent

		// \param [in] rhs - reference to the structure to perform comparison with
		// \return
		// - true if all members of the two structures are equal.
		// - false otherwise.
		bool operator==(const ShaderResourceDesc& rhs) const noexcept
		{
			return Type == rhs.Type && ArraySize == rhs.ArraySize && SafeStrEqual(Name, rhs.Name);
		}

	};


	// Describes the basic type of a shader code variable.
	enum SHADER_CODE_BASIC_TYPE : uint8 //
	{
		// The type is unknown.
		SHADER_CODE_BASIC_TYPE_UNKNOWN,

		// Void pointer.
		SHADER_CODE_BASIC_TYPE_VOID,

		// boolean (bool).
		SHADER_CODE_BASIC_TYPE_BOOL,

		// Integer (int).
		SHADER_CODE_BASIC_TYPE_INT,

		// 8-bit integer (int8).
		SHADER_CODE_BASIC_TYPE_INT8,

		// 16-bit integer (int16).
		SHADER_CODE_BASIC_TYPE_INT16,

		// 64-bit integer (int64).
		SHADER_CODE_BASIC_TYPE_INT64,

		// Unsigned integer (uint).
		SHADER_CODE_BASIC_TYPE_UINT,

		// 8-bit unsigned integer (uint8).
		SHADER_CODE_BASIC_TYPE_UINT8,

		// 16-bit unsigned integer (uint16).
		SHADER_CODE_BASIC_TYPE_UINT16,

		// 64-bit unsigned integer (uint64).
		SHADER_CODE_BASIC_TYPE_UINT64,

		// Floating-point number (float).
		SHADER_CODE_BASIC_TYPE_FLOAT,

		// 16-bit floating-point number (half).
		SHADER_CODE_BASIC_TYPE_FLOAT16,

		// Double-precision (64-bit) floating-point number (double).
		SHADER_CODE_BASIC_TYPE_DOUBLE,

		// 8-bit float (min8float).
		SHADER_CODE_BASIC_TYPE_MIN8FLOAT,

		// 10-bit float (min10float).
		SHADER_CODE_BASIC_TYPE_MIN10FLOAT,

		// 16-bit float (min16float).
		SHADER_CODE_BASIC_TYPE_MIN16FLOAT,

		// 12-bit int (min12int).
		SHADER_CODE_BASIC_TYPE_MIN12INT,

		// 16-bit int (min16int).
		SHADER_CODE_BASIC_TYPE_MIN16INT,

		// 16-bit unsigned int (min12uint).
		SHADER_CODE_BASIC_TYPE_MIN16UINT,

		// String (string).
		SHADER_CODE_BASIC_TYPE_STRING,

		SHADER_CODE_BASIC_TYPE_COUNT,
	};


	// Describes the class of a shader code variable.
	enum SHADER_CODE_VARIABLE_CLASS : uint8 //
	{
		// The variable class is unknown.
		SHADER_CODE_VARIABLE_CLASS_UNKNOWN,

		// The variable is a scalar.
		SHADER_CODE_VARIABLE_CLASS_SCALAR,

		// The variable is a vector.
		SHADER_CODE_VARIABLE_CLASS_VECTOR,

		// The variable is a row-major matrix.
		SHADER_CODE_VARIABLE_CLASS_MATRIX_ROWS,

		// The variable is a column-major matrix.
		SHADER_CODE_VARIABLE_CLASS_MATRIX_COLUMNS,

		// The variable is a structure.
		SHADER_CODE_VARIABLE_CLASS_STRUCT,

		SHADER_CODE_VARIABLE_CLASS_COUNT,
	};


	// Describes the shader code variable.
	struct ShaderCodeVariableDesc
	{
		// The variable name.
		const char* Name = nullptr;

		// The variable type name. May be null for basic types.
		const char* TypeName = nullptr;

		// Variable class, see shz::SHADER_CODE_VARIABLE_CLASS.
		SHADER_CODE_VARIABLE_CLASS Class = SHADER_CODE_VARIABLE_CLASS_UNKNOWN;

		// Basic data type, see shz::SHADER_CODE_BASIC_TYPE.
		SHADER_CODE_BASIC_TYPE BasicType = SHADER_CODE_BASIC_TYPE_UNKNOWN;

		// For a matrix type, the number of rows.

		// \note   For shaders compiled from GLSL, NumRows and NumColumns are swapped.
		uint8 NumRows = 0;

		// For a matrix type, the number of columns. For a vector, the number of components.

		// \note   For shaders compiled from GLSL, NumRows and NumColumns are swapped.
		uint8 NumColumns = 0;

		// Offset, in bytes, between the start of the parent structure and this variable.
		uint32 Offset = 0;

		// Array size.
		uint32 ArraySize = 0;

		// For a structure, the number of structure members; otherwise 0.
		uint32 NumMembers = 0;

		// For a structure, an array of NumMembers structure members.
		const struct ShaderCodeVariableDesc* pMembers = nullptr;

		constexpr ShaderCodeVariableDesc() noexcept
		{
		}

		constexpr ShaderCodeVariableDesc(
			const char* _Name,
			const char* _TypeName,
			SHADER_CODE_VARIABLE_CLASS _Class,
			SHADER_CODE_BASIC_TYPE     _BasicType,
			uint8                      _NumRows,
			uint8                      _NumColumns,
			uint32                     _Offset,
			uint32                     _ArraySize = 0) noexcept
			: Name(_Name)
			, TypeName(_TypeName)
			, Class(_Class)
			, BasicType(_BasicType)
			, NumRows(_NumRows)
			, NumColumns(_NumColumns)
			, Offset(_Offset)
			, ArraySize(_ArraySize)
		{
		}

		constexpr ShaderCodeVariableDesc(
			const char* _Name,
			const char* _TypeName,
			SHADER_CODE_BASIC_TYPE _BasicType,
			uint32                 _Offset,
			uint32                 _ArraySize = 0) noexcept
			: Name(_Name)
			, TypeName(_TypeName)
			, Class(SHADER_CODE_VARIABLE_CLASS_SCALAR)
			, BasicType(_BasicType)
			, NumRows(1)
			, NumColumns(1)
			, Offset(_Offset)
			, ArraySize(_ArraySize)
		{
		}

		constexpr ShaderCodeVariableDesc(
			const char* _Name,
			const char* _TypeName,
			uint32                        _NumMembers,
			const ShaderCodeVariableDesc* _pMembers,
			uint32                        _Offset,
			uint32                        _ArraySize = 0) noexcept
			: Name(_Name)
			, TypeName(_TypeName)
			, Class(SHADER_CODE_VARIABLE_CLASS_STRUCT)
			, Offset(_Offset)
			, ArraySize(_ArraySize)
			, NumMembers(_NumMembers)
			, pMembers(_pMembers)
		{
		}


		// Comparison operator tests if two structures are equivalent
		bool operator==(const ShaderCodeVariableDesc& rhs) const noexcept
		{

			if (!SafeStrEqual(Name, rhs.Name) ||
				!SafeStrEqual(TypeName, rhs.TypeName) ||
				Class != rhs.Class ||
				BasicType != rhs.BasicType ||
				NumRows != rhs.NumRows ||
				NumColumns != rhs.NumColumns ||
				ArraySize != rhs.ArraySize ||
				Offset != rhs.Offset ||
				NumMembers != rhs.NumMembers)
				return false;


			for (uint32 i = 0; i < NumMembers; ++i)
			{
				if (pMembers[i] != rhs.pMembers[i])
					return false;
			}

			return true;
		}
		bool operator!=(const ShaderCodeVariableDesc& rhs) const noexcept
		{
			return !(*this == rhs);
		}

	};


	// Describes a shader constant buffer.
	struct ShaderCodeBufferDesc
	{
		// Buffer size in bytes.
		uint32 Size = 0;

		// The number of variables in the buffer.
		uint32 NumVariables = 0;

		// An array of NumVariables variables, see shz::ShaderCodeVariableDesc.
		const ShaderCodeVariableDesc* pVariables = nullptr;

		constexpr ShaderCodeBufferDesc() noexcept
		{
		}


		constexpr ShaderCodeBufferDesc(
			uint32                        _Size,
			uint32                        _NumVariables,
			const ShaderCodeVariableDesc* _pVariables) noexcept
			: Size(_Size)
			, NumVariables(_NumVariables)
			, pVariables(_pVariables)
		{
		}

		// Comparison operator tests if two structures are equivalent
		bool operator==(const ShaderCodeBufferDesc& rhs) const noexcept
		{

			if (Size != rhs.Size ||
				NumVariables != rhs.NumVariables)
				return false;


			for (uint32 i = 0; i < NumVariables; ++i)
			{
				if (pVariables[i] != rhs.pVariables[i])
					return false;
			}

			return true;
		}
		bool operator!=(const ShaderCodeBufferDesc& rhs) const noexcept
		{
			return !(*this == rhs);
		}
	};


	// Shader interface
	struct SHZ_INTERFACE IShader : public IDeviceObject
	{
		// Returns the shader description
		virtual const ShaderDesc& GetDesc() const override = 0;

		// Returns the total number of shader resources
		virtual uint32 GetResourceCount() const = 0;

		// Returns the pointer to the array of shader resources
		virtual void GetResourceDesc(uint32 Index, ShaderResourceDesc& ResourceDesc) const = 0;

		// For a constant buffer resource, returns the buffer description. See shz::ShaderCodeBufferDesc.

		// \param [in] Index - Resource index, same as used by GetResourceDesc.
		//
		// \return     A pointer to ShaderCodeBufferDesc struct describing the constant buffer.
		//
		// This method requires that the `LoadConstantBufferReflection` flag was set to `true`
		// when the shader was created.
		virtual const ShaderCodeBufferDesc* GetConstantBufferDesc(uint32 Index) const = 0;

		// Returns the shader bytecode.

		// \param [out] ppBytecode - A pointer to the memory location where
		//                           a pointer to the byte code will be written.
		// \param [out] Size       - The size of the byte code.
		//
		// For OpenGL, this method returns the full GLSL source.
		//
		// The pointer returned by the method remains valid while the
		// shader object is alive. An application must NOT free the memory.
		virtual void GetBytecode(const void** ppBytecode, uint64& Size) const = 0;

		// Returns the shader status, see shz::SHADER_STATUS.

		// \param [in] WaitForCompletion - If true, the method will wait until the shader is compiled.
		//                                 If false, the method will return the shader status without waiting.
		// 							    This parameter is ignored if the shader was compiled synchronously.
		// \return     The shader status.
		virtual SHADER_STATUS GetStatus(bool WaitForCompletion = false) = 0;
	};


} // namespace shz
