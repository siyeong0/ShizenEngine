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
 // Definition of the shz::IRenderDevice interface and related data structures

#include "Primitives/Object.h"
#include "IEngineFactory.h"
#include "GraphicsTypes.h"
#include "Constants.h"
#include "IBuffer.h"
#include "InputLayout.h"
#include "IShader.h"
#include "ITexture.h"
#include "ISampler.h"
#include "IResourceMapping.h"
#include "ITextureView.h"
#include "IBufferView.h"
#include "IPipelineState.h"
#include "IPipelineStateCache.h"
#include "IFence.h"
#include "IQuery.h"
#include "IRenderPass.h"
#include "IFramebuffer.h"
#include "IBottomLevelAS.h"
#include "ITopLevelAS.h"
#include "IShaderBindingTable.h"
#include "IPipelineResourceSignature.h"
#include "IDeviceMemory.h"
#include "IDeviceContext.h"

#include "DepthStencilState.h"
#include "RasterizerState.h"
#include "BlendState.h"

namespace shz
{

	// {F0E9B607-AE33-4B2B-B1AF-A8B2C3104022}
	static constexpr INTERFACE_ID IID_RenderDevice =
	{ 0xf0e9b607, 0xae33, 0x4b2b, {0xb1, 0xaf, 0xa8, 0xb2, 0xc3, 0x10, 0x40, 0x22} };

	// Render device interface
	struct SHZ_INTERFACE IRenderDevice : public IObject
	{
		// Creates a new buffer object

		// \param [in] BuffDesc   - Buffer description, see shz::BufferDesc for details.
		// \param [in] pBuffData  - Pointer to shz::BufferData structure that describes
		//                          initial buffer data or nullptr if no data is provided.
		//                          Immutable buffers (USAGE_IMMUTABLE) must be initialized at creation time.
		// \param [out] ppBuffer  - Address of the memory location where a pointer to the
		//                          buffer interface will be written. The function calls AddRef(),
		//                          so that the new buffer will have one reference and must be
		//                          released by a call to Release().
		//
		// Size of a uniform buffer (shz::BIND_UNIFORM_BUFFER) must be multiple of 16.
		//
		// Stride of a formatted buffer will be computed automatically from the format if
		// ElementByteStride member of buffer description is set to default value (0).
		virtual void CreateBuffer(
			const BufferDesc& BuffDesc,
			const BufferData* pBuffData,
			IBuffer** ppBuffer) = 0;

		// Creates a new shader object

		// \param [in]  ShaderCI - Shader create info, see shz::ShaderCreateInfo for details.
		// \param [out] ppShader - Address of the memory location where a pointer to the
		//                         shader interface will be written.
		//                         The function calls AddRef(), so that the new object will have
		//                         one reference.
		// \param [out] ppCompilerOutput - Address of the memory location where a pointer to the
		//                                 the compiler output data blob will be written.
		//                                 If null, the compiler output will be ignored.
		//
		// The buffer returned in ppCompilerOutput contains two null-terminated strings.
		// The first one is the compiler output message. The second one is the full
		// shader source code including definitions added by the engine. The data blob
		// object must be released by the client.
		virtual void CreateShader(
			const ShaderCreateInfo& ShaderCI,
			IShader** ppShader,
			IDataBlob** ppCompilerOutput = nullptr) = 0;

		// Creates a new texture object

		// \param [in] TexDesc - Texture description, see shz::TextureDesc for details.
		// \param [in] pData   - Pointer to shz::TextureData structure that describes
		//                       initial texture data or nullptr if no data is provided.
		//                       Immutable textures (USAGE_IMMUTABLE) must be initialized at creation time.
		//
		// \param [out] ppTexture - Address of the memory location where a pointer to the
		//                          texture interface will be written.
		//                          The function calls AddRef(), so that the new object will have
		//                          one reference.
		//
		// To create all mip levels, set the TexDesc.MipLevels to zero.
		//
		// Multisampled resources cannot be initialized with data when they are created.
		//
		// If initial data is provided, number of subresources must exactly match the number
		// of subresources in the texture (which is the number of mip levels times the number of array slices.
		// For a 3D texture, this is just the number of mip levels).
		// For example, for a 15 x 6 x 2 2D texture array, the following array of subresources should be
		// provided:
		//
		//     15x6, 7x3, 3x1, 1x1, 15x6, 7x3, 3x1, 1x1.
		// 
		// For a 15 x 6 x 4 3D texture, the following array of subresources should be provided:
		//
		//     15x6x4, 7x3x2, 3x1x1, 1x1x1
		virtual void CreateTexture(
			const TextureDesc& TexDesc,
			const TextureData* pData,
			ITexture** ppTexture) = 0;

		// Creates a new sampler object

		// \param [in]  SamDesc   - Sampler description, see shz::SamplerDesc for details.
		// \param [out] ppSampler - Address of the memory location where a pointer to the
		//                          sampler interface will be written.
		//                          The function calls AddRef(), so that the new object will have
		//                          one reference.
		//
		// If an application attempts to create a sampler interface with the same attributes
		// as an existing interface, the same interface will be returned.
		//
		// In D3D11, 4096 unique sampler state objects can be created on a device at a time.
		virtual void CreateSampler(const SamplerDesc& SamDesc, ISampler** ppSampler) = 0;

		// Creates a new resource mapping

		// \param [in]  ResMappingCI - Resource mapping create info, see shz::ResourceMappingCreateInfo for details.
		// \param [out] ppMapping    - Address of the memory location where a pointer to the
		//                             resource mapping interface will be written.
		//                             The function calls AddRef(), so that the new object will have
		//                             one reference.
		virtual void CreateResourceMapping(const ResourceMappingCreateInfo& ResMappingCI, IResourceMapping** ppMapping) = 0;

		// Creates a new graphics pipeline state object

		// \param [in]  PSOCreateInfo   - Graphics pipeline state create info, see shz::GraphicsPipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the
		//                                pipeline state interface will be written.
		//                                The function calls AddRef(), so that the new object will have
		//                                one reference.
		virtual void CreateGraphicsPipelineState(const GraphicsPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Creates a new compute pipeline state object

		// \param [in]  PSOCreateInfo   - Compute pipeline state create info, see shz::ComputePipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the
		//                                pipeline state interface will be written.
		//                                The function calls AddRef(), so that the new object will have
		//                                one reference.
		virtual void CreateComputePipelineState(const ComputePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Creates a new ray tracing pipeline state object

		// \param [in]  PSOCreateInfo   - Ray tracing pipeline state create info, see shz::RayTracingPipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the
		//                                pipeline state interface will be written.
		//                                The function calls AddRef(), so that the new object will have
		//                                one reference.
		virtual void CreateRayTracingPipelineState(const RayTracingPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Creates a new tile pipeline state object

		// \param [in]  PSOCreateInfo   - Tile pipeline state create info, see shz::TilePipelineStateCreateInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the
		//                                pipeline state interface will be written.
		//                                The function calls AddRef(), so that the new object will have
		//                                one reference.
		virtual void CreateTilePipelineState(const TilePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState) = 0;

		// Creates a new fence object

		// \param [in]  Desc    - Fence description, see shz::FenceDesc for details.
		// \param [out] ppFence - Address of the memory location where a pointer to the
		//                        fence interface will be written.
		//                        The function calls AddRef(), so that the new object will have
		//                        one reference.
		virtual void CreateFence(const FenceDesc& Desc, IFence** ppFence) = 0;


		// Creates a new query object

		// \param [in]  Desc    - Query description, see shz::QueryDesc for details.
		// \param [out] ppQuery - Address of the memory location where a pointer to the
		//                        query interface will be written.
		//                        The function calls AddRef(), so that the new object will have
		//                        one reference.
		virtual void CreateQuery(const QueryDesc& Desc, IQuery** ppQuery) = 0;


		// Creates a render pass object

		// \param [in]  Desc         - Render pass description, see shz::RenderPassDesc for details.
		// \param [out] ppRenderPass - Address of the memory location where a pointer to the
		//                             render pass interface will be written.
		//                             The function calls AddRef(), so that the new object will have
		//                             one reference.
		virtual void CreateRenderPass(const RenderPassDesc& Desc, IRenderPass** ppRenderPass) = 0;



		// Creates a framebuffer object

		// \param [in]  Desc          - Framebuffer description, see shz::FramebufferDesc for details.
		// \param [out] ppFramebuffer - Address of the memory location where a pointer to the
		//                              framebuffer interface will be written.
		//                              The function calls AddRef(), so that the new object will have
		//                              one reference.
		virtual void CreateFramebuffer(const FramebufferDesc& Desc, IFramebuffer** ppFramebuffer) = 0;


		// Creates a bottom-level acceleration structure object (BLAS).

		// \param [in]  Desc    - BLAS description, see shz::BottomLevelASDesc for details.
		// \param [out] ppBLAS  - Address of the memory location where a pointer to the
		//                        BLAS interface will be written.
		//                        The function calls AddRef(), so that the new object will have
		//                        one reference.
		virtual void CreateBLAS(const BottomLevelASDesc& Desc, IBottomLevelAS** ppBLAS) = 0;


		// Creates a top-level acceleration structure object (TLAS).

		// \param [in]  Desc    - TLAS description, see shz::TopLevelASDesc for details.
		// \param [out] ppTLAS  - Address of the memory location where a pointer to the
		//                        TLAS interface will be written.
		//                        The function calls AddRef(), so that the new object will have
		//                        one reference.
		virtual void CreateTLAS(const TopLevelASDesc& Desc, ITopLevelAS** ppTLAS) = 0;


		// Creates a shader resource binding table object (SBT).

		// \param [in]  Desc    - SBT description, see shz::ShaderBindingTableDesc for details.
		// \param [out] ppSBT   - Address of the memory location where a pointer to the
		//                        SBT interface will be written.
		//                        The function calls AddRef(), so that the new object will have
		//                        one reference.
		virtual void CreateSBT(const ShaderBindingTableDesc& Desc, IShaderBindingTable** ppSBT) = 0;

		// Creates a pipeline resource signature object.

		// \param [in]  Desc         - Resource signature description, see shz::PipelineResourceSignatureDesc for details.
		// \param [out] ppSignature  - Address of the memory location where a pointer to the
		//                             pipeline resource signature interface will be written.
		//                             The function calls AddRef(), so that the new object will have
		//                             one reference.
		virtual void CreatePipelineResourceSignature(const PipelineResourceSignatureDesc& Desc, IPipelineResourceSignature** ppSignature) = 0;


		// Creates a device memory object.

		// \param [in]  CreateInfo - Device memory create info, see shz::DeviceMemoryCreateInfo for details.
		// \param [out] ppMemory   - Address of the memory location where a pointer to the
		//                           device memory interface will be written.
		//                           The function calls AddRef(), so that the new object will have
		//                           one reference.
		virtual void CreateDeviceMemory(const DeviceMemoryCreateInfo& CreateInfo, IDeviceMemory** ppMemory) = 0;


		// Creates a pipeline state cache object.

		// \param [in]  CreateInfo - Pipeline state cache create info, see shz::PiplineStateCacheCreateInfo for details.
		// \param [out] ppPSOCache - Address of the memory location where a pointer to the
		//                           pipeline state cache interface will be written.
		//                           The function calls AddRef(), so that the new object will have
		//                           one reference.
		//
		// On devices that don't support pipeline state caches (e.g. Direct3D11, OpenGL),
		// the method will silently do nothing.
		virtual void CreatePipelineStateCache(const PipelineStateCacheCreateInfo& CreateInfo, IPipelineStateCache** ppPSOCache) = 0;


		// Creates a deferred context.

		// \param [out] ppContext - Address of the memory location where a pointer to the
		//                          deferred context interface will be written.
		// 
		// \remarks    Deferred contexts are not supported in OpenGL and WebGPU backends.
		virtual void CreateDeferredContext(IDeviceContext** ppContext) = 0;

		// Returns the device information, see shz::RenderDeviceInfo for details.
		virtual const RenderDeviceInfo& GetDeviceInfo() const = 0;

		// Returns the graphics adapter information, see shz::GraphicsAdapterInfo for details.
		virtual const GraphicsAdapterInfo& GetAdapterInfo() const = 0;

		// Returns the basic texture format information.

		// See shz::TextureFormatInfo for details on the provided information.
		// \param [in] TexFormat - Texture format for which to provide the information
		// \return Const reference to the TextureFormatInfo structure containing the
		//         texture format description.
		//
		// \remarks This method must be externally synchronized.
		virtual const TextureFormatInfo& GetTextureFormatInfo(TEXTURE_FORMAT TexFormat) const = 0;


		// Returns the extended texture format information.

		// See shz::TextureFormatInfoExt for details on the provided information.
		//
		// \param [in] TexFormat - Texture format for which to provide the information
		// \return Const reference to the TextureFormatInfoExt structure containing the
		//         extended texture format description.
		//
		// The first time this method is called for a particular format, it may be
		// considerably slower than GetTextureFormatInfo(). If you do not require
		// extended information, call GetTextureFormatInfo() instead.
		//
		// \remarks This method must be externally synchronized.
		virtual const TextureFormatInfoExt& GetTextureFormatInfoExt(TEXTURE_FORMAT TexFormat) = 0;


		// Returns the sparse texture format info for the given texture format, resource dimension and sample count.
		virtual SparseTextureFormatInfo GetSparseTextureFormatInfo(
			TEXTURE_FORMAT TexFormat,
			RESOURCE_DIMENSION Dimension,
			uint32 SampleCount) const = 0;

		// Purges device release queues and releases all stale resources.
		// This method is automatically called by ISwapChain::Present() of the primary swap chain.
		// \param [in]  ForceRelease - Forces release of all objects. Use this option with
		//                             great care only if you are sure the resources are not
		//                             in use by the GPU (such as when the device has just been idled).
		virtual void ReleaseStaleResources(bool ForceRelease = false) = 0;


		// Waits until all outstanding operations on the GPU are complete.

		// \note The method blocks the execution of the calling thread until the GPU is idle.
		//
		// The method does not flush immediate contexts, so it will only wait for commands that
		// the contexts using IDeviceContext::Flush() if it needs to make sure all recorded commands
		// have been previously submitted for execution. An application should explicitly flush
		// are complete when the method returns.
		virtual void IdleGPU() = 0;


		// Returns engine factory this device was created from.

		// This method does not increment the reference counter of the returned interface,
		// so an application must not call Release().
		virtual IEngineFactory* GetEngineFactory() const = 0;


		// Returns a pointer to the shader compilation thread pool.

		// This method does not increment the reference counter of the returned interface,
		// so an application must not call Release().
		virtual IThreadPool* GetShaderCompilationThreadPool() const = 0;

		// Overloaded alias for CreateGraphicsPipelineState.
		void CreatePipelineState(const GraphicsPipelineStateCreateInfo& CI, IPipelineState** ppPipelineState)
		{
			CreateGraphicsPipelineState(CI, ppPipelineState);
		}
		// Overloaded alias for CreateComputePipelineState.
		void CreatePipelineState(const ComputePipelineStateCreateInfo& CI, IPipelineState** ppPipelineState)
		{
			CreateComputePipelineState(CI, ppPipelineState);
		}
		// Overloaded alias for CreateRayTracingPipelineState.
		void CreatePipelineState(const RayTracingPipelineStateCreateInfo& CI, IPipelineState** ppPipelineState)
		{
			CreateRayTracingPipelineState(CI, ppPipelineState);
		}
		// Overloaded alias for CreateTilePipelineState.
		void CreatePipelineState(const TilePipelineStateCreateInfo& CI, IPipelineState** ppPipelineState)
		{
			CreateTilePipelineState(CI, ppPipelineState);
		}
	};


} // namespace shz
