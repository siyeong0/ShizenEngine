/*
 *  Copyright 2019-2025 Diligent Graphics LLC
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
 // Defines shz::IRenderStateCache interface

#include "Engine/RHI/Interface/IRenderDevice.h"

namespace shz
{

	// Render state cache logging level.
	enum RENDER_STATE_CACHE_LOG_LEVEL : uint8
	{
		// Logging is disabled.
		RENDER_STATE_CACHE_LOG_LEVEL_DISABLED,

		// Normal logging level.
		RENDER_STATE_CACHE_LOG_LEVEL_NORMAL,

		// Verbose logging level.
		RENDER_STATE_CACHE_LOG_LEVEL_VERBOSE
	};

	// Hash mode used by the render state cache to identify unique files.
	enum RENDER_STATE_CACHE_FILE_HASH_MODE : uint8
	{
		// Hash files by their content.

		// This is the most reliable method, but it requires reading
		// the entire file contents, as well as all included files,
		// which may be time-consuming.
		RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT,

		// Hash files by their names.

		// This method is very fast, but it it does not detect
		// changes in the file contents.
		// 
		// This mode is not compatible with shader hot reloading.
		// 
		// \note If the file is modified after it has been cached,
		//       the cache will not detect the change and will continue
		//       to use the old cached version.
		RENDER_STATE_CACHE_FILE_HASH_MODE_BY_NAME
	};



	// Render state cache create information.
	struct RenderStateCacheCreateInfo
	{
		// A pointer to the render device, must not be null.
		IRenderDevice* pDevice = nullptr;

		// Archiver factory, must not be null.

		// Use `LoadAndGetArchiverFactory()` from `ArchiverFactoryLoader.h` to create the factory.
		struct IArchiverFactory* pArchiverFactory = nullptr;

		// Logging level, see shz::RENDER_STATE_CACHE_LOG_LEVEL.
		RENDER_STATE_CACHE_LOG_LEVEL LogLevel = RENDER_STATE_CACHE_LOG_LEVEL_NORMAL;

		// Source file hash mode, see shz::RENDER_STATE_CACHE_FILE_HASH_MODE.
		RENDER_STATE_CACHE_FILE_HASH_MODE FileHashMode = RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT;

		// Whether to enable hot shader and pipeline state reloading.

		// When enabled, the cache will support the `Reload()` method
		// that detects changes in the original shader source files
		// and reloads the corresponding shaders and pipeline states.
		//
		// Hot reloading requires that the file hash mode is
		// `shz::RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT`.
		//
		// \note   Hot reloading introduces some overhead and should
		//         generally be disabled in production builds.
		bool EnableHotReload = false;

		// Whether to optimize OpenGL shaders.

		// This option directly controls the value of the
		// SerializationDeviceGLInfo::OptimizeShaders member
		// of the internal serialization device.
		bool OptimizeGLShaders = true;

		// Optional shader source input stream factory to use when reloading
		// shaders. If null, original source factory will be used.
		IShaderSourceInputStreamFactory* pReloadSource = nullptr;

		constexpr RenderStateCacheCreateInfo() noexcept
		{
		}

		constexpr explicit RenderStateCacheCreateInfo(
			IRenderDevice* _pDevice,
			struct IArchiverFactory* _pArchiverFactory,
			RENDER_STATE_CACHE_LOG_LEVEL      _LogLevel = RenderStateCacheCreateInfo{}.LogLevel,
			RENDER_STATE_CACHE_FILE_HASH_MODE _FileHashMode = RenderStateCacheCreateInfo{}.FileHashMode,
			bool                              _EnableHotReload = RenderStateCacheCreateInfo{}.EnableHotReload,
			bool                              _OptimizeGLShaders = RenderStateCacheCreateInfo{}.OptimizeGLShaders,
			IShaderSourceInputStreamFactory* _pReloadSource = RenderStateCacheCreateInfo{}.pReloadSource) noexcept
			: pDevice(_pDevice)
			, pArchiverFactory(_pArchiverFactory)
			, LogLevel(_LogLevel)
			, FileHashMode(_FileHashMode)
			, EnableHotReload(_EnableHotReload)
			, OptimizeGLShaders(_OptimizeGLShaders)
			, pReloadSource(_pReloadSource)
		{
		}
	};

	// Type of the callback function called by the IRenderStateCache::Reload method.
	typedef void(SHZ_CALL_TYPE* ReloadGraphicsPipelineCallbackType)(const char* PipelineName, GraphicsPipelineDesc& GraphicsDesc, void* pUserData);

	// {5B356268-256C-401F-BDE2-B9832157141A}
	static constexpr INTERFACE_ID IID_RenderStateCache =
	{ 0x5b356268, 0x256c, 0x401f, {0xbd, 0xe2, 0xb9, 0x83, 0x21, 0x57, 0x14, 0x1a} };


	// Render state cache interface.
	struct SHZ_INTERFACE IRenderStateCache : public IObject
	{
		// Loads the cache contents.

		// \param [in] pCacheData     - A pointer to the cache data to load objects from.
		// \param [in] ContentVersion - The expected version of the content in the cache.
		//                              If the version of the content in the cache does not
		//                              match the expected version, the method will fail.
		//                              If default value is used (`~0u` aka `0xFFFFFFFF`), the version
		//                              will not be checked.
		// \param [in] MakeCopy       - Whether to make a copy of the data blob, or use the
		//                              the original contents.
		// \return     true if the data were loaded successfully, and false otherwise.
		//
		// If the data were not copied, the cache will keep a strong reference
		// to the pCacheData data blob. It will be kept alive until the cache object
		// is released or the Reset() method is called.
		//
		// \warning    If the data were loaded without making a copy, the application
		//             must not modify it while it is in use by the cache object.
		// 
		// \note       This method is not thread-safe and must not be called simultaneously
		//             with other methods.
		virtual bool Load(const IDataBlob* pCacheData, uint32 ContentVersion = ~0u, bool MakeCopy = false) = 0;

		// Creates a shader object from cached data.

		// \param [in]  ShaderCI - Shader create info, see shz::ShaderCreateInfo for details.
		// \param [out] ppShader - Address of the memory location where a pointer to the created
		//                         shader object will be written.
		//
		// \return     true if the shader was loaded from the cache, and false otherwise.
		virtual bool CreateShader(const ShaderCreateInfo& ShaderCI, IShader** ppShader) = 0;

		// Creates a graphics pipeline state object from cached data.

		// \param [in]  PSOCreateInfo   - Graphics pipeline state create info, see shz::GraphicsPipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the created
		//                                pipeline state object will be written.
		//
		// \return     true if the pipeline state was loaded from the cache, and false otherwise.
		virtual bool CreateGraphicsPipelineState(const GraphicsPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Creates a compute pipeline state object from cached data.

		// \param [in]  PSOCreateInfo   - Compute pipeline state create info, see shz::ComputePipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the created
		//                                pipeline state object will be written.
		//
		// \return     true if the pipeline state was loaded from the cache, and false otherwise.
		virtual bool CreateComputePipelineState(const ComputePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Creates a ray tracing pipeline state object from cached data.

		// \param [in]  PSOCreateInfo   - Ray tracing pipeline state create info, see shz::RayTracingPipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the created
		//                                pipeline state object will be written.
		//
		// \return     true if the pipeline state was loaded from the cache, and false otherwise.
		virtual bool CreateRayTracingPipelineState(const RayTracingPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Creates a tile pipeline state object from cached data.

		// \param [in]  PSOCreateInfo   - Tile pipeline state create info, see shz::TilePipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the created
		//                                pipeline state object will be written.
		//
		// \return     true if the pipeline state was loaded from the cache, and false otherwise.
		virtual bool CreateTilePipelineState(const TilePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Writes cache contents to a memory blob.

		// \param [in]   ContentVersion - The version of the content to write.
		// \param [out]  ppBlob         - Address of the memory location where a pointer to the created
		//                                data blob will be written.
		//
		// \return     true if the data was written successfully, and false otherwise.
		//
		// \remarks    If ContentVersion is `~0u` (aka `0xFFFFFFFF`), the version of the
		//             previously loaded content will be used, or 0 if none was loaded.
		virtual bool WriteToBlob(uint32 ContentVersion, IDataBlob** ppBlob) = 0;

		// Writes cache contents to a file stream.

		// \param [in]  ContentVersion - The version of the content to write.
		// \param [in]  pStream        - Pointer to the IFileStream interface to use for writing.
		//
		// \return     true if the data was written successfully, and false otherwise.
		//
		// \remarks    If ContentVersion is `~0u` (aka `0xFFFFFFFF`), the version of the
		//             previously loaded content will be used, or 0 if none was loaded.
		virtual bool WriteToStream(uint32 ContentVersion, IFileStream* pStream) = 0;


		// Resets the cache to default state.
		virtual void Reset() = 0;

		// Reloads render states in the cache.

		// \param [in]  ReloadGraphicsPipeline - An optional callback function that will be called by the render state cache
		//                                       to let the application modify graphics pipeline state info before creating new
		//                                       pipeline.
		// \param [in]  pUserData              - A pointer to the user-specific data to pass to ReloadGraphicsPipeline callback.
		//
		// \return     The total number of render states (shaders and pipelines) that were reloaded.
		//
		// Reloading is only enabled if the cache was created with the `EnableHotReload` member of
		// `shz::RenderStateCacheCreateInfo` struct set to true.
		virtual uint32 Reload(ReloadGraphicsPipelineCallbackType ReloadGraphicsPipeline = nullptr, void* pUserData = nullptr) = 0;

		// Returns the content version of the cache data.

		// If no data has been loaded, returns `~0u` (aka `0xFFFFFFFF`).
		virtual uint32 GetContentVersion() const = 0;


		// Returns the reload version of the cache data.

		// The reload version is incremented every time the cache is reloaded.
		virtual uint32 GetReloadVersion() const = 0;
	};


	void CreateRenderStateCache(const RenderStateCacheCreateInfo& CreateInfo, IRenderStateCache** ppCache);


} // namespace shz
