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
 // Definition of the shz::IDearchiver interface and related data structures

#include "Primitives/DataBlob.h"
#include "IPipelineResourceSignature.h"
#include "IPipelineState.h"

namespace shz
{

	// Shader unpack parameters
	struct ShaderUnpackInfo
	{
		struct IRenderDevice* pDevice = nullptr;

		// Name of the shader to unpack.
		const Char* Name = nullptr;

		// An optional function to be called by the dearchiver to let the application modify
		// the shader description.
		void (*ModifyShaderDesc)(ShaderDesc& Desc, void* pUserData) = nullptr;

		// A pointer to the user data to pass to the ModifyShaderDesc function.
		void* pUserData = nullptr;
	};


	// Resource signature unpack parameters
	struct ResourceSignatureUnpackInfo
	{
		struct IRenderDevice* pDevice = nullptr;

		// Name of the signature to unpack. If there is only
		// one signature in the archive, the name may be null.
		const Char* Name = nullptr;

		// Shader resource binding allocation granularity.

		// This member defines the allocation granularity for internal resources required by
		// the shader resource binding object instances.
		uint32 SRBAllocationGranularity = 1;
	};

	// Pipeline state archive flags
	enum PSO_ARCHIVE_FLAGS : uint32
	{
		// No flags are set
		PSO_ARCHIVE_FLAG_NONE = 0u,

		// By default, shader reflection information will be preserved
		// during the PSO serialization. When this flag is specified,
		// it will be stripped from the bytecode. This will reduce
		// the binary size, but also make run-time checks not possible.
		// Applications should generally use this flag for Release builds.
		// TODO: this flag may need to be defined when archive is created
		// to avoid situations where the same byte code is archived with
		// and without reflection from different PSOs.
		PSO_ARCHIVE_FLAG_STRIP_REFLECTION = 1u << 0,

		// Do not archive signatures used by the pipeline state.
		//
		// \note   The flag only applies to explicit signatures.
		//         Implicit signatures are always packed.
		PSO_ARCHIVE_FLAG_DO_NOT_PACK_SIGNATURES = 1u << 1
	};
	DEFINE_FLAG_ENUM_OPERATORS(PSO_ARCHIVE_FLAGS);


	// Pipeline state unpack flags
	enum PSO_UNPACK_FLAGS : uint32
	{
		// No flags are set
		PSO_UNPACK_FLAG_NONE = 0u,

		// Do not perform validation when unpacking the pipeline state.
		// (TODO: maybe this flag is not needed as validation will not be performed
		//        if there is no reflection information anyway).

		// \remarks Parameter validation will only be performed if the PSO
		//          was serialized without stripping the reflection. If
		//          reflection was stripped, validation will never be performed
		//          and this flag will have no effect.
		PSO_UNPACK_FLAG_NO_VALIDATION = 1u << 0,
	};
	DEFINE_FLAG_ENUM_OPERATORS(PSO_UNPACK_FLAGS);


	// Pipeline state unpack parameters
	struct PipelineStateUnpackInfo
	{
		struct IRenderDevice* pDevice = nullptr;

		// Name of the PSO to unpack. If there is only
		// one PSO in the archive, the name may be null.
		const Char* Name = nullptr;

		// The type of the pipeline state to unpack, see shz::PIPELINE_TYPE.
		PIPELINE_TYPE PipelineType = PIPELINE_TYPE_INVALID;

		// Shader resource binding allocation granularity

		// This member defines allocation granularity for internal resources required by the shader resource
		// binding object instances.
		// Has no effect if the PSO is created with explicit pipeline resource signature(s).
		uint32 SRBAllocationGranularity = 1;

		// Defines which immediate contexts are allowed to execute commands that use this pipeline state.

		// When ImmediateContextMask contains a bit at position n, the pipeline state may be
		// used in the immediate context with index n directly (see DeviceContextDesc::ContextId).
		// It may also be used in a command list recorded by a deferred context that will be executed
		// through that immediate context.
		//
		// \remarks    Only specify these bits that will indicate those immediate contexts where the PSO
		//             will actually be used. Do not set unnecessary bits as this will result in extra overhead.
		uint64 ImmediateContextMask = 1;

		// Optional PSO cache.
		IPipelineStateCache* pCache = nullptr;

		// An optional function to be called by the dearchiver to let the application modify the pipeline state create info.

		// An application should check the pipeline type (PipelineCI.Desc.PipelineType) and cast
		// the reference to the appropriate PSO create info struct, e.g. for PIPELINE_TYPE_GRAPHICS:
		//
		//     auto& GraphicsPipelineCI = static_cast<GraphicsPipelineStateCreateInfo>(PipelineCI);
		//
		// Modifying graphics pipeline states (e.g. rasterizer, depth-stencil, blend, render
		// target formats, etc.) is the most expected usage of the callback.
		//
		// The following members of the structure must not be modified:
		// - `PipelineCI.PSODesc.PipelineType`
		// - `PipelineCI.PSODesc.ResourceLayout`
		// - `PipelineCI.ppResourceSignatures`
		// - `PipelineCI.ResourceSignaturesCount`
		//
		// An application may modify shader pointers (e.g. GraphicsPipelineCI.pVS), but it must
		// ensure that the shader layout is compatible with the pipeline state, otherwise hard-to-debug
		// errors will occur.
		void (*ModifyPipelineStateCreateInfo)(PipelineStateCreateInfo& PipelineCI, void* pUserData) = nullptr;

		// A pointer to the user data to pass to the ModifyPipelineStateCreateInfo function.
		void* pUserData = nullptr;
	};


	// Render pass unpack parameters
	struct RenderPassUnpackInfo
	{
		struct IRenderDevice* pDevice = nullptr;

		// Name of the render pass to unpack.
		const Char* Name = nullptr;

		// An optional function to be called by the dearchiver to let the application modify
		// the render pass description.
		void (*ModifyRenderPassDesc)(RenderPassDesc& Desc, void* pUserData) = nullptr;

		// A pointer to the user data to pass to the ModifyRenderPassDesc function.
		void* pUserData = nullptr;
	};

	// {ACB3F67A-CE3B-4212-9592-879122D3C191}
	static constexpr INTERFACE_ID IID_Dearchiver =
	{ 0xacb3f67a, 0xce3b, 0x4212, {0x95, 0x92, 0x87, 0x91, 0x22, 0xd3, 0xc1, 0x91} };


	// Dearchiver interface
	struct SHZ_INTERFACE IDearchiver : public IObject
	{
		// Lodas a device object archive.

		// \param [in] pArchive       - A pointer to the source raw data to load objects from.
		// \param [in] ContentVersion - The expected version of the content in the archive.
		//                              If the version of the content in the archive does not
		//                              match the expected version, the method will fail.
		//                              If default value is used (~0u aka 0xFFFFFFFF), the version
		//                              will not be checked.
		// \param [in] MakeCopy       - Whether to make a copy of the archive, or use the
		//                              the original contents.
		//
		// \return     true if the archive has been loaded successfully, and false otherwise.
		//
		// \note       If the archive was not copied, the dearchiver will keep a strong reference
		//             to the pArchive data blob. It will be kept alive until the dearchiver object
		//             is released or the Reset() method is called.
		//
		// \warning    If the archive was loaded without making a copy, the application
		//             must not modify its contents while it is in use by the dearchiver.
		// 
		// \warning    This method is not thread-safe and must not be called simultaneously
		//             with other methods.
		virtual bool LoadArchive(const IDataBlob* pArchive, uint32 ContentVersion = ~0u, bool MakeCopy = false) = 0;

		// Unpacks a shader from the device object archive.

		// \param [in]  UnpackInfo - Shader unpack info, see shz::ShaderUnpackInfo.
		// \param [out] ppShader   - Address of the memory location where a pointer to the
		//                           unpacked shader object will be stored.
		//                           The function calls AddRef(), so that the shader object will have
		//                           one reference.
		//
		// \note   This method is thread-safe.
		virtual void UnpackShader(const ShaderUnpackInfo& UnpackInfo, IShader** ppShader) = 0;

		// Unpacks a pipeline state object from the device object archive.

		// \param [in]  UnpackInfo - Pipeline state unpack info, see shz::PipelineStateUnpackInfo.
		// \param [out] ppPSO      - Address of the memory location where a pointer to the
		//                           unpacked pipeline state object will be stored.
		//                           The function calls AddRef(), so that the PSO will have
		//                           one reference.
		//
		// \note   Resource signatures used by the PSO will be unpacked from the same archive.
		//
		// \note   This method is thread-safe.
		virtual void UnpackPipelineState(const PipelineStateUnpackInfo& UnpackInfo, IPipelineState** ppPSO) = 0;

		// Unpacks resource signature from the device object archive.

		// \param [in]  UnpackInfo  - Resource signature unpack info, see shz::ResourceSignatureUnpackInfo.
		// \param [out] ppSignature - Address of the memory location where a pointer to the
		//                            unpacked pipeline resource signature object will be stored.
		//                            The function calls AddRef(), so that the resource signature will have
		//                            one reference.
		//
		// \note   This method is thread-safe.
		virtual void UnpackResourceSignature(const ResourceSignatureUnpackInfo& UnpackInfo, IPipelineResourceSignature** ppSignature) = 0;

		// Unpacks render pass from the device object archive.

		// \param [in]  UnpackInfo  - Render pass unpack info, see shz::RenderPassUnpackInfo.
		// \param [out] ppSignature - Address of the memory location where a pointer to the
		//                            unpacked render pass object will be stored.
		//                            The function calls AddRef(), so that the render pass will have
		//                            one reference.
		//
		// \note   This method is thread-safe.
		virtual void UnpackRenderPass(const RenderPassUnpackInfo& UnpackInfo, IRenderPass** ppRP) = 0;

		// Writes archive data to the data blob.

		// \param [in] ppArchive - Memory location where a pointer to the archive data blob will be written.
		// \return     true if the archive data was written successfully, and false otherwise.
		//
		// \note       This method combines all archives loaded by the dearchiver into a single archive.
		//
		// \warning    This method is not thread-safe and must not be called simultaneously
		//             with other methods.
		virtual bool Store(IDataBlob** ppArchive) const = 0;

		// Resets the dearchiver state and releases all loaded objects.
		//
		// \warning    This method is not thread-safe and must not be called simultaneously
		//             with other methods.
		virtual void Reset() = 0;

		// Returns the content version of the archive.
		// If no data has been loaded, returns ~0u (aka 0xFFFFFFFF).
		virtual uint32 GetContentVersion() const = 0;
	};



}
