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
 // Defines shz::IEngineFactory interface

#include "Primitives/Object.h"
#include "Primitives/DebugOutput.h"
#include "Primitives/DataBlob.h"
#include "Primitives/IMemoryAllocator.h"
#include "GraphicsTypes.h"


#if PLATFORM_ANDROID
struct ANativeActivity;
struct AAssetManager;
#endif

namespace shz
{
	struct IShaderSourceInputStreamFactory;
	struct IDearchiver;

	// {D932B052-4ED6-4729-A532-F31DEEC100F3}
	static constexpr INTERFACE_ID IID_EngineFactory =
	{ 0xd932b052, 0x4ed6, 0x4729, {0xa5, 0x32, 0xf3, 0x1d, 0xee, 0xc1, 0x0, 0xf3} };

	// Dearchiver create information
	struct DearchiverCreateInfo
	{
		void* pDummy = nullptr;
	};

	// Engine factory base interface
	struct SHZ_INTERFACE IEngineFactory : public IObject
	{
		// Returns API info structure, see shz::APIInfo.
		virtual const APIInfo& GetAPIInfo() const = 0;

		// Creates default shader source input stream factory

		// \param [in]  SearchDirectories           - Semicolon-separated list of search directories.
		// \param [out] ppShaderSourceStreamFactory - Memory address where the pointer to the shader source stream factory will be written.
		virtual void CreateDefaultShaderSourceStreamFactory(
			const Char* SearchDirectories,
			struct IShaderSourceInputStreamFactory** ppShaderSourceFactory) const = 0;

		// Creates a data blob.

		// \param [in]  InitialSize - The size of the internal data buffer.
		// \param [in]  pData       - Pointer to the data to write to the internal buffer.
		//                            If null, no data will be written.
		// \param [out] ppDataBlob  - Memory address where the pointer to the data blob will be written.
		virtual void CreateDataBlob(
			size_t InitialSize,
			const void* pData,
			IDataBlob** ppDataBlob) const = 0;

		// Enumerates adapters available on this machine.

		// \param [in]     MinVersion  - Minimum required API version (feature level for Direct3D).
		// \param [in,out] NumAdapters - The number of adapters. If Adapters is null, this value
		//                               will be overwritten with the number of adapters available
		//                               on this system. If Adapters is not null, this value should
		//                               contain the maximum number of elements reserved in the array
		//                               pointed to by Adapters. In the latter case, this value
		//                               is overwritten with the actual number of elements written to
		//                               Adapters.
		// \param [out]    Adapters - Pointer to the array containing adapter information. If
		//                            null is provided, the number of available adapters is
		//                            written to NumAdapters.
		//
		// \note OpenGL backend only supports one device; features and properties will have limited information.
		virtual void EnumerateAdapters(
			Version MinVersion,
			uint32& NumAdapters,
			GraphicsAdapterInfo* Adapters) const = 0;

		// Creates a dearchiver object.

		// \param [in]  CreateInfo   - Dearchiver create info, see shz::DearchiverCreateInfo for details.
		// \param [out] ppDearchiver - Address of the memory location where a pointer to IDearchiver
		//                             interface will be written.
		//                             The function calls AddRef(), so that the new object will have
		//                             one reference.
		virtual void CreateDearchiver(const DearchiverCreateInfo& CreateInfo, struct IDearchiver** ppDearchiver) const = 0;


		// Sets a user-provided debug message callback.

		// \param [in] MessageCallback - Debug message callback function to use instead of the default one.
		//
		// MessageCallback is a global setting that applies to the entire execution unit
		// (executable or shared library that contains the engine implementation).
		virtual void SetMessageCallback(DebugMessageCallbackType MessageCallback) const = 0;


		// Sets whether to break program execution on assertion failure.

		// \param [in] BreakOnError - Whether to break on assertion failure.
		//
		// BreakOnError is a global setting that applies to the entire execution unit 
		// (executable or shared library that contains the engine implementation).
		virtual void SetBreakOnError(bool BreakOnError) const = 0;


		// Sets the memory allocator to be used by the engine.

		// \param [in] pAllocator - Pointer to the memory allocator.
		//
		// The allocator is a global setting that applies to the entire execution unit
		// (executable or shared library that contains the engine implementation).
		//
		// The allocator should be set before any other factory method is called and
		// should not be changed afterwards.
		// The allocator object must remain valid for the lifetime of the
		// engine until all engine objects are destroyed.
		virtual void SetMemoryAllocator(IMemoryAllocator* pAllocator) const = 0;

#if PLATFORM_ANDROID
		// On Android platform, it is necessary to initialize the file system before
		// CreateDefaultShaderSourceStreamFactory() method can be called.

		// \param [in] AssetManager     - A pointer to the asset manager (AAssetManager).
		// \param [in] ExternalFilesDir - External files directory.
		// \param [in] OutputFilesDir   - Output files directory.
		//
		// \remarks See AndroidFileSystem::Init.
		virtual void InitAndroidFileSystem(
			struct AAssetManager* AssetManager,
			const char* ExternalFilesDir = nullptr,
			const char* OutputFilesDir = nullptr) const = 0;
#endif
	};


} // namespace shz
