/*
/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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
 // Defines graphics engine utilities

#include "Engine/RHI/Interface/IShader.h"

namespace shz
{
	// Shader source file substitute info.
	struct ShaderSourceFileSubstitueInfo
	{
		// Source file name.
		const Char* Name = nullptr;

		// Substitute file name.
		const Char* Substitute = nullptr;

		constexpr ShaderSourceFileSubstitueInfo() noexcept
		{
		}

		constexpr ShaderSourceFileSubstitueInfo(const Char* _Name, const Char* _Substitute) noexcept
			: Name{ _Name }
			, Substitute{ _Substitute }
		{
		}
	};


	// Compound shader source factory create info.
	struct CompoundShaderSourceFactoryCreateInfo
	{
		// An array of shader source input stream factories.
		IShaderSourceInputStreamFactory** ppFactories = nullptr;

		// The number of factories in ppFactories array.
		uint32 NumFactories = 0;

		// An array of shader source file substitutes.
		const ShaderSourceFileSubstitueInfo* pFileSubstitutes = nullptr;

		// The number of file substitutes in pFileSubstitutes array.
		uint32 NumFileSubstitutes = 0;

		constexpr CompoundShaderSourceFactoryCreateInfo() noexcept
		{
		}

		constexpr CompoundShaderSourceFactoryCreateInfo(
			IShaderSourceInputStreamFactory** _ppFactories,
			uint32                               _NumFactories,
			const ShaderSourceFileSubstitueInfo* _pFileSubstitutes = nullptr,
			uint32                               _NumFileSubstitutes = 0) noexcept
			: ppFactories{ _ppFactories }
			, NumFactories{ _NumFactories }
			, pFileSubstitutes{ _pFileSubstitutes }
			, NumFileSubstitutes{ _NumFileSubstitutes }
		{
		}
	};


	// Creates a compound shader source factory.

	// \param [in]  CreateInfo - Compound shader source factory create info, see shz::CompoundShaderSourceFactoryCreateInfo.
	// \param [out] ppFactory  - Address of the memory location where the pointer to the created factory will be written.
	//
	// Compound shader source stream factory is a wrapper around multiple shader source stream factories.
	// It is used to combine multiple shader source stream factories into a single one. When a source file
	// is requested, the factory will iterate over all factories in the array and return the first one that
	// returns a non-null stream.
	//
	// The factory also allows substituting source file names. This is useful when the same shader source
	// is used for multiple shaders, but some of them require a modified version of the source.
	void CreateCompoundShaderSourceFactory(const CompoundShaderSourceFactoryCreateInfo& CreateInfo,
		IShaderSourceInputStreamFactory** ppFactory);


	// Shader source file info.
	struct MemoryShaderSourceFileInfo
	{
		// File name.
		const Char* Name = nullptr;

		// Shader source.
		const Char* pData = nullptr;

		// Shader source length. If 0, the length will be calculated automatically
		// assuming that the source is null-terminated.
		uint32 Length = 0;

		constexpr MemoryShaderSourceFileInfo() noexcept
		{
		}

		constexpr MemoryShaderSourceFileInfo(
			const Char* _Name,
			const Char* _pData,
			uint32      _Length = 0) noexcept
			: Name{ _Name }
			, pData{ _pData }
			, Length{ _Length }
		{
		}

		MemoryShaderSourceFileInfo(const Char* _Name, const String& Data) noexcept
			: Name{ _Name }
			, pData{ Data.c_str() }
			, Length{ static_cast<uint32>(Data.length()) }
		{
		}

	};


	// Memory shader source factory create info.
	struct MemoryShaderSourceFactoryCreateInfo
	{
		// An array of shader source files.
		const MemoryShaderSourceFileInfo* pSources = nullptr;

		// The number of files in pSources array.
		uint32 NumSources = 0;

		// Whether to copy shader sources. If false, the factory will assume that
		// the source data will remain valid for the lifetime of the factory.
		bool CopySources = false;

		constexpr MemoryShaderSourceFactoryCreateInfo() noexcept
		{
		}

		constexpr MemoryShaderSourceFactoryCreateInfo(
			const MemoryShaderSourceFileInfo* _pSources,
			uint32                            _NumSources,
			bool                              _CopySources = false) noexcept
			: pSources{ _pSources }
			, NumSources{ _NumSources }
			, CopySources{ _CopySources }
		{
		}
	};

	// Crates a memory shader source factory.
	//
	// \param [in]  CreateInfo - Memory shader source factory create info, see shz::MemoryShaderSourceFactoryCreateInfo.
	// \param [out] ppFactory  - Address of the memory location where the pointer to the created factory will be written.
	void CreateMemoryShaderSourceFactory(const MemoryShaderSourceFactoryCreateInfo& CreateInfo,
		IShaderSourceInputStreamFactory** ppFactory);

} // namespace shz
