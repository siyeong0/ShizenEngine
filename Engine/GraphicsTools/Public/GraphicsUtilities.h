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
 // Defines graphics engine utilities

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/Core/Common/Public/GeometryPrimitives.h"

namespace shz
{

	void CreateUniformBuffer(IRenderDevice* pDevice,
		uint64                          Size,
		const Char* Name,
		IBuffer** ppBuffer,
		USAGE                           Usage = USAGE_DYNAMIC,
		BIND_FLAGS                      BindFlags = BIND_UNIFORM_BUFFER,
		CPU_ACCESS_FLAGS                CPUAccessFlags = CPU_ACCESS_WRITE,
		void* pInitialData = nullptr);


	void GenerateCheckerBoardPattern(uint32         Width,
		uint32         Height,
		TEXTURE_FORMAT Fmt,
		uint32         HorzCells,
		uint32         VertCells,
		uint8* pData,
		uint64         StrideInBytes);

	// Coarse mip filter type
	enum MIP_FILTER_TYPE : uint8
	{
		// Default filter type: BOX_AVERAGE for UNORM/SNORM and FP formats, and
		// MOST_FREQUENT for UINT/SINT formats.
		MIP_FILTER_TYPE_DEFAULT = 0,

		// 2x2 box average.
		MIP_FILTER_TYPE_BOX_AVERAGE,

		// Use the most frequent element from the 2x2 box.
		// This filter does not introduce new values and should be used
		// for integer textures that contain non-filterable data (e.g. indices).
		MIP_FILTER_TYPE_MOST_FREQUENT
	};

	// ComputeMipLevel function attributes
	struct ComputeMipLevelAttribs
	{
		// Texture format.
		TEXTURE_FORMAT Format = TEX_FORMAT_UNKNOWN;

		// Fine mip level width.
		uint32 FineMipWidth = 0;

		// Fine mip level height.
		uint32 FineMipHeight = 0;

		// Pointer to the fine mip level data
		const void* pFineMipData = nullptr;

		// Fine mip level data stride, in bytes.
		size_t FineMipStride = 0;

		// Pointer to the coarse mip level data
		void* pCoarseMipData = nullptr;

		// Coarse mip level data stride, in bytes.
		size_t CoarseMipStride = 0;

		// Filter type.
		MIP_FILTER_TYPE FilterType = MIP_FILTER_TYPE_DEFAULT;

		// Alpha cutoff value.

		// When AlphaCutoff is not 0, alpha channel is remapped as follows:
		//
		//     A_new = max(A_old; 1/3 * A_old + 2/3 * AlphaCutoff)
		float AlphaCutoff = 0;

		constexpr ComputeMipLevelAttribs() noexcept {}

		constexpr ComputeMipLevelAttribs(
			TEXTURE_FORMAT _Format,
			uint32 _FineMipWidth,
			uint32 _FineMipHeight,
			const void* _pFineMipData,
			size_t _FineMipStride,
			void* _pCoarseMipData,
			size_t _CoarseMipStride,
			MIP_FILTER_TYPE _FilterType = ComputeMipLevelAttribs{}.FilterType,
			float _AlphaCutoff = ComputeMipLevelAttribs{}.AlphaCutoff) noexcept
			: Format{ _Format }
			, FineMipWidth{ _FineMipWidth }
			, FineMipHeight{ _FineMipHeight }
			, pFineMipData{ _pFineMipData }
			, FineMipStride{ _FineMipStride }
			, pCoarseMipData{ _pCoarseMipData }
			, CoarseMipStride{ _CoarseMipStride }
			, FilterType{ _FilterType }
			, AlphaCutoff{ _AlphaCutoff }
		{
		}
	};


	void ComputeMipLevel(const ComputeMipLevelAttribs& Attribs);


	// Creates a sparse texture in Metal backend.

	// \param [in]  pDevice   - A pointer to the render device.
	// \param [in]  TexDesc   - Texture description.
	// \param [in]  pMemory   - A pointer to the device memory.
	// \param [out] ppTexture - Address of the memory location where a pointer to the
	//                          sparse texture will be written.
	//
	// If `pDevice` is a pointer to Metal device (shz::IRenderDeviceMtl), this function
	// creates a sparse texture using IRenderDeviceMtl::CreateSparseTexture method.
	// Otherwise, it does nothing.
	void CreateSparseTextureMtl(IRenderDevice* pDevice,
		const TextureDesc& TexDesc,
		IDeviceMemory* pMemory,
		ITexture** ppTexture);


	// Returns default shader resource view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetDefaultSRV(ITexture* pTexture);

	// Returns default render target view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetDefaultRTV(ITexture* pTexture);

	// Returns default depth-stencil view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetDefaultDSV(ITexture* pTexture);

	// Returns default unordered access view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetDefaultUAV(ITexture* pTexture);

	// Returns default shader resource view of a buffer.

	// If the buffer is null, returns null.
	IBufferView* GetDefaultSRV(IBuffer* pBuffer);

	// Returns default unordered access view of a buffer.

	// If the buffer is null, returns null.
	IBufferView* GetDefaultUAV(IBuffer* pBuffer);


	// Returns default shader resource view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetTextureDefaultSRV(IObject* pTexture);

	// Returns default render target view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetTextureDefaultRTV(IObject* pTexture);

	// Returns default depth-stencil view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetTextureDefaultDSV(IObject* pTexture);

	// Returns default unordered access view of a texture.

	// If the texture is null, returns null.
	ITextureView* GetTextureDefaultUAV(IObject* pTexture);

	// Returns default shader resource view of a buffer.

	// If the buffer is null, returns null.
	IBufferView* GetBufferDefaultSRV(IObject* pBuffer);

	// Returns default unordered access view of a buffer.

	// If the buffer is null, returns null.
	IBufferView* GetBufferDefaultUAV(IObject* pBuffer);


	// For WebGPU shaders, returns the suffix to append to the name of emulated array variables to get
	// the indexed array element name.
	// For other shader types, returns null.
	const char* GetWebGPUEmulatedArrayIndexSuffix(IShader* pShader);

	// Returns the native texture format (e.g. DXGI_FORMAT, VkFormat) for the given texture format and device type.
	int64_t GetNativeTextureFormat(TEXTURE_FORMAT TexFormat, enum RENDER_DEVICE_TYPE DeviceType);

	// Returns the texture format for the given native format (e.g. DXGI_FORMAT, VkFormat) and device type.
	TEXTURE_FORMAT GetTextureFormatFromNative(int64_t NativeFormat, enum RENDER_DEVICE_TYPE DeviceType);


	// Geometry primitive buffers creation info
	struct GeometryPrimitiveBuffersCreateInfo
	{
		// Vertex buffer usage.
		USAGE VertexBufferUsage = USAGE_DEFAULT;

		// Index buffer usage.
		USAGE IndexBufferUsage = USAGE_DEFAULT;

		// Vertex buffer bind flags.
		BIND_FLAGS VertexBufferBindFlags = BIND_VERTEX_BUFFER;

		// Index buffer bind flags.
		BIND_FLAGS IndexBufferBindFlags = BIND_INDEX_BUFFER;

		// Vertex buffer mode.
		BUFFER_MODE VertexBufferMode = BUFFER_MODE_UNDEFINED;

		// Index buffer mode.
		BUFFER_MODE IndexBufferMode = BUFFER_MODE_UNDEFINED;

		// Vertex buffer CPU access flags.
		CPU_ACCESS_FLAGS VertexBufferCPUAccessFlags = CPU_ACCESS_NONE;

		// Index buffer CPU access flags.
		CPU_ACCESS_FLAGS IndexBufferCPUAccessFlags = CPU_ACCESS_NONE;
	};



	// Creates vertex and index buffers for a geometry primitive (see shz::CreateGeometryPrimitive)

	// \param [in]  pDevice   - A pointer to the render device that will be used to create the buffers.
	// \param [in]  Attribs   - Geometry primitive attributes, see shz::GeometryPrimitiveAttributes.
	// \param [in]  pBufferCI - Optional buffer create info, see shz::GeometryPrimitiveBufferCreateInfo.
	//                          If null, default values are used.
	// \param [out] ppVertices - Address of the memory location where the pointer to the vertex buffer will be stored.
	// \param [out] ppIndices  - Address of the memory location where the pointer to the index buffer will be stored.
	// \param [out] pInfo      - A pointer to the structure that will receive information about the created geometry primitive.
	//                           See shz::GeometryPrimitiveInfo.
	void CreateGeometryPrimitiveBuffers(IRenderDevice* pDevice,
		const GeometryPrimitiveAttributes& Attribs,
		const GeometryPrimitiveBuffersCreateInfo* pBufferCI,
		IBuffer** ppVertices,
		IBuffer** ppIndices,
		GeometryPrimitiveInfo* pInfo = nullptr);


	// Returns the DirectX shader compiler interface associated with the specified render device.

	// \param [in] pDevice - A pointer to the render device.
	// \return     A pointer to the DirectX shader compiler interface.
	//             If the device does not support DirectX shader compiler,
	//             or if the compiler is not loaded, returns null.
	struct IDXCompiler* GetDeviceDXCompiler(IRenderDevice* pDevice);

} // namespace shz
