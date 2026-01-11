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
 // Defines shz::ISerializationDevice interface

#include "Engine/Core/Common/Public/StringTools.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IPipelineResourceSignature.h"
#include "Archiver.h"

namespace shz
{
	// {205BB0B2-0966-4F51-9380-46EE5BCED28B}
	static constexpr INTERFACE_ID IID_SerializationDevice =
	{ 0x205bb0b2, 0x966, 0x4f51, {0x93, 0x80, 0x46, 0xee, 0x5b, 0xce, 0xd2, 0x8b} };

	// Shader archive info
	struct ShaderArchiveInfo
	{
		// Bitset of shz::ARCHIVE_DEVICE_DATA_FLAGS.

		// Specifies for which backends the shader data will be serialized.
		ARCHIVE_DEVICE_DATA_FLAGS DeviceFlags = ARCHIVE_DEVICE_DATA_FLAG_NONE;
	};

	// Pipeline resource signature archive info
	struct ResourceSignatureArchiveInfo
	{
		// Bitset of shz::ARCHIVE_DEVICE_DATA_FLAGS.

		// Specifies for which backends the resource signature data will be serialized.
		ARCHIVE_DEVICE_DATA_FLAGS DeviceFlags = ARCHIVE_DEVICE_DATA_FLAG_NONE;
	};

	// Pipeline state archive info
	struct PipelineStateArchiveInfo
	{
		// Pipeline state archive flags, see shz::PSO_ARCHIVE_FLAGS.
		PSO_ARCHIVE_FLAGS PSOFlags = PSO_ARCHIVE_FLAG_NONE;

		// Bitset of shz::ARCHIVE_DEVICE_DATA_FLAGS.

		// Specifies for which backends the pipeline state data will be serialized.
		ARCHIVE_DEVICE_DATA_FLAGS DeviceFlags = ARCHIVE_DEVICE_DATA_FLAG_NONE;
	};


	// Contains attributes to calculate pipeline resource bindings
	struct PipelineResourceBindingAttribs
	{
		// An array of `ResourceSignaturesCount` shader resource signatures that
		// define the layout of shader resources in this pipeline state object.

		// See shz::IPipelineResourceSignature.
		IPipelineResourceSignature** ppResourceSignatures = nullptr;

		// The number of elements in `ppResourceSignatures` array.
		uint32                       ResourceSignaturesCount = 0;

		// The number of render targets, only for graphics pipeline.

		// \note Required for Direct3D11 graphics pipelines that use UAVs.
		uint32                       NumRenderTargets = 0;

		// The number of vertex buffers, only for graphics pipeline.

		// \note Required for Metal.
		uint32                       NumVertexBuffers = 0;

		// Vertex buffer names.

		// \note Required for Metal.
		Char const* const* VertexBufferNames = nullptr;

		// Combination of shader stages.
		SHADER_TYPE                  ShaderStages = SHADER_TYPE_UNKNOWN;

		// Device type for which resource binding will be calculated.
		enum RENDER_DEVICE_TYPE      DeviceType = RENDER_DEVICE_TYPE_UNDEFINED;
	};

	// Pipeline resource binding
	struct PipelineResourceBinding
	{
		// Resource name
		const Char* Name = nullptr;

		// Resource type, see shz::SHADER_RESOURCE_TYPE.
		SHADER_RESOURCE_TYPE ResourceType = SHADER_RESOURCE_TYPE_UNKNOWN;

		// Shader resource stages, see shz::SHADER_TYPE.
		SHADER_TYPE          ShaderStages = SHADER_TYPE_UNKNOWN;

		// Shader register space.
		uint16               Space = 0;

		// Shader register.
		uint32               Register = 0;

		// Array size
		uint32               ArraySize = 0;
	};


	// Serialization device interface
	struct SHZ_INTERFACE ISerializationDevice : public IRenderDevice
	{
		// Creates a serialized shader.

		// \param [in]  ShaderCI    - Shader create info, see shz::ShaderCreateInfo for details.
		// \param [in]  ArchiveInfo - Shader archive info, see shz::ShaderArchiveInfo for details.
		// \param [out] ppShader    - Address of the memory location where a pointer to the
		//                            shader interface will be written.
		// \param [out] ppCompilerOutput - Address of the memory location where a pointer to the
		//                                 shader compiler output will be written.
		//                                 If null, the output will be ignored.
		// \note
		//     The method is thread-safe and may be called from multiple threads simultaneously.
		virtual void CreateShader(
			const ShaderCreateInfo& ShaderCI,
			const ShaderArchiveInfo& ArchiveInfo,
			IShader** ppShader,
			IDataBlob** ppCompilerOutput = nullptr) = 0;

		// Creates a serialized pipeline resource signature.

		// \param [in]  Desc        - Pipeline resource signature description, see shz::PipelineResourceSignatureDesc for details.
		// \param [in]  ArchiveInfo - Signature archive info, see shz::ResourceSignatureArchiveInfo for details.
		// \param [out] ppShader    - Address of the memory location where a pointer to the serialized
		//                            pipeline resource signature object will be written.
		//
		// \note
		//     The method is thread-safe and may be called from multiple threads simultaneously.
		virtual void CreatePipelineResourceSignature(
			const PipelineResourceSignatureDesc& Desc,
			const ResourceSignatureArchiveInfo& ArchiveInfo,
			IPipelineResourceSignature** ppSignature) = 0;

		// Creates a serialized graphics pipeline state.

		// \param [in]  PSOCreateInfo   - Graphics pipeline state create info, see shz::GraphicsPipelineStateCreateInfo for details.
		// \param [in]  ArchiveInfo     - Pipeline state archive info, see shz::PipelineStateArchiveInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the serialized
		//                                pipeline state object will be written.
		//
		// \note
		//     All objects that PSOCreateInfo references (shaders, render pass, resource signatures) must be
		//     serialized objects created by the same serialization device.
		//
		//     The method is thread-safe and may be called from multiple threads simultaneously.
		virtual void CreateGraphicsPipelineState(
			const GraphicsPipelineStateCreateInfo& PSOCreateInfo,
			const PipelineStateArchiveInfo& ArchiveInfo,
			IPipelineState** ppPipelineState) = 0;

		// Creates a serialized compute pipeline state.

		// \param [in]  PSOCreateInfo   - Compute pipeline state create info, see shz::ComputePipelineStateCreateInfo for details.
		// \param [in]  ArchiveInfo     - Pipeline state archive info, see shz::PipelineStateArchiveInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the serialized
		//                                pipeline state object will be written.
		//
		// \note
		//     All objects that PSOCreateInfo references (shaders, resource signatures) must be
		//     serialized objects created by the same serialization device.
		//
		//     The method is thread-safe and may be called from multiple threads simultaneously.
		virtual void CreateComputePipelineState(
			const ComputePipelineStateCreateInfo& PSOCreateInfo,
			const PipelineStateArchiveInfo& ArchiveInfo,
			IPipelineState** ppPipelineState) = 0;

		// Creates a serialized ray tracing pipeline state.

		// \param [in]  PSOCreateInfo   - Ray tracing pipeline state create info, see shz::RayTracingPipelineStateCreateInfo for details.
		// \param [in]  ArchiveInfo     - Pipeline state archive info, see shz::PipelineStateArchiveInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the serialized
		//                                pipeline state object will be written.
		//
		// \note
		//     All objects that PSOCreateInfo references (shaders, resource signatures) must be
		//     serialized objects created by the same serialization device.
		//
		//     The method is thread-safe and may be called from multiple threads simultaneously.
		virtual void CreateRayTracingPipelineState(
			const RayTracingPipelineStateCreateInfo& PSOCreateInfo,
			const PipelineStateArchiveInfo& ArchiveInfo,
			IPipelineState** ppPipelineState) = 0;

		// Creates a serialized tile pipeline state.

		// \param [in]  PSOCreateInfo   - Tile pipeline state create info, see shz::TilePipelineStateCreateInfo for details.
		// \param [in]  ArchiveInfo     - Pipeline state archive info, see shz::PipelineStateArchiveInfo for details.
		// \param [out] ppPipelineState - Address of the memory location where a pointer to the serialized
		//                                pipeline state interface will be written.
		//
		// \note
		//     All objects that PSOCreateInfo references (shaders, resource signatures) must be
		//     serialized objects created by the same serialization device.
		//
		//     The method is thread-safe and may be called from multiple threads simultaneously.
		virtual void CreateTilePipelineState(
			const TilePipelineStateCreateInfo& PSOCreateInfo,
			const PipelineStateArchiveInfo& ArchiveInfo,
			IPipelineState** ppPipelineState) = 0;


		// Populates an array of pipeline resource bindings.
		virtual void GetPipelineResourceBindings(
			const PipelineResourceBindingAttribs& Attribs,
			uint32& NumBindings,
			const PipelineResourceBinding*& pBindings) = 0;


		// Returns a combination of supported device flags, see shz::ARCHIVE_DEVICE_DATA_FLAGS.
		virtual ARCHIVE_DEVICE_DATA_FLAGS GetSupportedDeviceFlags() const = 0;

		// Adds a optional render device that will be used to initialize device-specific objects that
		// may be used for rendering (e.g. shaders).
		// For example, a shader object retrieved with ISerializedShader::GetDeviceShader() will be
		// suitable for rendering.
		virtual void AddRenderDevice(IRenderDevice* pDevice) = 0;

		// Overloaded alias for ISerializationDevice::CreateGraphicsPipelineState.
		void CreatePipelineState(const GraphicsPipelineStateCreateInfo& CI, const PipelineStateArchiveInfo& ArchiveInfo, IPipelineState** ppPipelineState)
		{
			CreateGraphicsPipelineState(CI, ArchiveInfo, ppPipelineState);
		}
		// Overloaded alias for ISerializationDevice::CreateComputePipelineState.
		void CreatePipelineState(const ComputePipelineStateCreateInfo& CI, const PipelineStateArchiveInfo& ArchiveInfo, IPipelineState** ppPipelineState)
		{
			CreateComputePipelineState(CI, ArchiveInfo, ppPipelineState);
		}
		// Overloaded alias for ISerializationDevice::CreateRayTracingPipelineState.
		void CreatePipelineState(const RayTracingPipelineStateCreateInfo& CI, const PipelineStateArchiveInfo& ArchiveInfo, IPipelineState** ppPipelineState)
		{
			CreateRayTracingPipelineState(CI, ArchiveInfo, ppPipelineState);
		}
		// Overloaded alias for ISerializationDevice::CreateTilePipelineState.
		void CreatePipelineState(const TilePipelineStateCreateInfo& CI, const PipelineStateArchiveInfo& ArchiveInfo, IPipelineState** ppPipelineState)
		{
			CreateTilePipelineState(CI, ArchiveInfo, ppPipelineState);
		}
	};


} // namespace shz
