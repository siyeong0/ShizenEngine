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
 // Definition of the shz::IDeviceContext interface and related data structures

#include "Primitives/Object.h"
#include "Primitives/FlagEnum.h"
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
#include "DepthStencilState.h"
#include "BlendState.h"
#include "IPipelineState.h"
#include "IFence.h"
#include "IQuery.h"
#include "IRenderPass.h"
#include "IFramebuffer.h"
#include "ICommandList.h"
#include "ISwapChain.h"
#include "IBottomLevelAS.h"
#include "ITopLevelAS.h"
#include "IShaderBindingTable.h"
#include "IDeviceMemory.h"
#include "ICommandQueue.h"

namespace shz
{

	// {DC92711B-A1BE-4319-B2BD-C662D1CC19E4}
	static constexpr INTERFACE_ID IID_DeviceContext =
	{ 0xdc92711b, 0xa1be, 0x4319, {0xb2, 0xbd, 0xc6, 0x62, 0xd1, 0xcc, 0x19, 0xe4} };

	// Device context description.
	struct DeviceContextDesc
	{
		// Device context name.

		// This name is what was specified in ImmediateContextCreateInfo::Name when the engine was initialized.
		const Char* Name = nullptr;

		// Command queue type that this context uses.

		// For immediate contexts, this type matches GraphicsAdapterInfo::Queues[QueueId].QueueType.
		// For deferred contexts, the type is only defined between IDeviceContext::Begin and IDeviceContext::FinishCommandList
		// calls and matches the type of the immediate context where the command list will be executed.
		COMMAND_QUEUE_TYPE QueueType = COMMAND_QUEUE_TYPE_UNKNOWN;

		// Indicates if this is a deferred context.
		bool IsDeferred = false;

		// Device context ID. This value corresponds to the index of the device context
		// in ppContexts array when the engine was initialized.
		// When starting recording commands with a deferred context, the context id
		// of the immediate context where the command list will be executed should be
		// given to IDeviceContext::Begin() method.
		uint8 ContextId = 0;

		// Hardware queue index in GraphicsAdapterInfo::Queues array.

		// This member is only defined for immediate contexts and matches
		// QueueId member of ImmediateContextCreateInfo struct that was used to
		// initialize the context.
		//
		// - Vulkan backend: same as queue family index.
		// - Direct3D12 backend:
		//   - 0 - Graphics queue
		//   - 1 - Compute queue
		//   - 2 - Transfer queue
		// - Metal backend: index of the unique command queue.
		uint8 QueueId = DEFAULT_QUEUE_ID;


		// Required texture granularity for copy operations, for a transfer queue.

		// For graphics and compute queues, the granularity is always {1,1,1}.
		// For transfer queues, an application must align the texture offsets and sizes
		// by the granularity defined by this member.
		//
		// For deferred contexts, this member is only defined between IDeviceContext::Begin() and
		// IDeviceContext::FinishCommandList() calls and matches the texture copy granularity of
		// the immediate context where the command list will be executed.
		uint32 TextureCopyGranularity[3] = {};

		constexpr DeviceContextDesc() noexcept {}

		// Initializes the structure with user-specified values.
		constexpr DeviceContextDesc(
			const Char* _Name,
			COMMAND_QUEUE_TYPE _QueueType,
			bool _IsDeferred,
			uint32 _ContextId,
			uint32 _QueueId = DeviceContextDesc{}.QueueId) noexcept
			: Name(_Name)
			, QueueType(_QueueType)
			, IsDeferred(_IsDeferred)
			, ContextId(static_cast<decltype(ContextId)>(_ContextId))
			, QueueId(static_cast<decltype(QueueId)>(_QueueId))
		{
			if (!IsDeferred)
			{
				TextureCopyGranularity[0] = 1;
				TextureCopyGranularity[1] = 1;
				TextureCopyGranularity[2] = 1;
			}
			else
			{
				// For deferred contexts texture copy granularity is set by IDeviceContext::Begin() method.
			}
		}
	};


	// Draw command flags
	enum DRAW_FLAGS : uint8
	{
		// No flags.
		DRAW_FLAG_NONE = 0u,

		// Verify the state of index and vertex buffers (if any) used by the draw
		// command. State validation is only performed in debug and development builds
		// and the flag has no effect in release build.
		DRAW_FLAG_VERIFY_STATES = 1u << 0u,

		// Verify correctness of parameters passed to the draw command.
		//
		// \remarks This flag only has effect in debug and development builds.
		//          Verification is always disabled in release configuration.
		DRAW_FLAG_VERIFY_DRAW_ATTRIBS = 1u << 1u,

		// Perform all state validation checks
		//
		// \remarks This flag only has effect in debug and development builds.
		//          Verification is always disabled in release configuration.
		DRAW_FLAG_VERIFY_ALL = DRAW_FLAG_VERIFY_STATES | DRAW_FLAG_VERIFY_DRAW_ATTRIBS,

		// Indicates that none of the dynamic resource buffers used by the draw command
		// have been modified by the CPU since the last command.
		//
		// This flag should be used to improve performance when an application issues a
		// series of draw commands that use the same pipeline state and shader resources and
		// no dynamic buffers (constant or bound as shader resources) are updated between the
		// commands.
		// Any buffer variable not created with `SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS` or
		// `PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS` flags is counted as dynamic.
		// The flag has no effect on dynamic vertex and index buffers.
		//
		//  **Details**
		//
		//  D3D12 and Vulkan backends have to perform some work to make data in buffers
		//  available to draw commands. When a dynamic buffer is mapped, the engine allocates
		//  new memory and assigns a new GPU address to this buffer. When a draw command is issued,
		//  this GPU address needs to be used. By default the engine assumes that the CPU may
		//  map the buffer before any command (to write new transformation matrices for example)
		//  and that all GPU addresses need to always be refreshed. This is not always the case,
		//  and the application may use the flag to inform the engine that the data in the buffer
		//  stay intact to avoid extra work.\n
		//  Note that after a new PSO is bound or an SRB is committed, the engine will always set all
		//  required buffer addresses/offsets regardless of the flag. The flag will only take effect
		//  on the second and subsequent draw calls that use the same PSO and SRB.\n
		//  The flag has no effect in D3D11 and OpenGL backends.
		//
		//  **Implementation details**
		//
		//  Vulkan backend allocates `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC` descriptors for all uniform (constant),
		//  buffers and `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC` descriptors for storage buffers.
		//  Note that HLSL structured buffers are mapped to read-only storage buffers in SPIRV and RW buffers
		//  are mapped to RW-storage buffers.
		//  By default, all dynamic descriptor sets that have dynamic buffers bound are updated every time a draw command is
		//  issued (see PipelineStateVkImpl::BindDescriptorSetsWithDynamicOffsets). When `DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT`
		//  is specified, dynamic descriptor sets are only bound by the first draw command that uses the PSO and the SRB.
		//  The flag avoids binding descriptors with the same offsets if none of the dynamic offsets have changed.
		//
		//  Direct3D12 backend binds constant buffers to root views. By default the engine assumes that virtual GPU addresses
		//  of all dynamic buffers may change between the draw commands and always binds dynamic buffers to root views
		//  (see RootSignature::CommitRootViews). When `DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT` is set, root views are only bound
		//  by the first draw command that uses the PSO + SRB pair. The flag avoids setting the same GPU virtual addresses when
		//  they stay unchanged.
		DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT = 1u << 2u
	};
	DEFINE_FLAG_ENUM_OPERATORS(DRAW_FLAGS);


	// Defines resource state transition mode performed by various commands.

	// Refer to http://diligentgraphics.com/2018/12/09/resource-state-management/ for detailed explanation
	// of resource state management in Diligent Engine.
	enum RESOURCE_STATE_TRANSITION_MODE : uint8
	{
		// Perform no state transitions and no state validation.
		// Resource states are not accessed (either read or written) by the command.
		RESOURCE_STATE_TRANSITION_MODE_NONE = 0,

		// Transition resources to the states required by the specific command.
		// Resources in unknown state are ignored.
		//
		// Any method that uses this mode may alter the state of the resources it works with.
		// As automatic state management is not thread-safe, no other thread is allowed to read
		// or write the state of the resources being transitioned.
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using IDeviceContext::TransitionResourceStates() method.
		//
		// \note    If a resource is used in multiple threads by multiple contexts, there will be race condition accessing
		//          internal resource state. An application should use manual resource state management in this case.
		RESOURCE_STATE_TRANSITION_MODE_TRANSITION,

		// Do not transition, but verify that states are correct.
		// No validation is performed if the state is unknown to the engine.
		// This mode only has effect in debug and development builds. No validation
		// is performed in release build.
		//
		// \note    Any method that uses this mode will read the state of resources it works with.
		//          As automatic state management is not thread-safe, no other thread is allowed to alter
		//          the state of resources being used by the command. It is safe to read these states.
		RESOURCE_STATE_TRANSITION_MODE_VERIFY
	};


	// Defines the draw command attributes.

	// This structure is used by IDeviceContext::Draw().
	struct DrawAttribs
	{
		// The number of vertices to draw.
		uint32 NumVertices = 0;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// The number of instances to draw.

		// If more than one instance is specified, instanced draw call will be performed.
		uint32 NumInstances = 1;

		// LOCATION (or INDEX, but NOT the byte offset) of the first vertex in the
		// vertex buffer to start reading vertices from.
		uint32 StartVertexLocation = 0;

		// LOCATION (or INDEX, but NOT the byte offset) in the vertex buffer to start
		// reading instance data from.
		uint32 FirstInstanceLocation = 0;


		// Initializes the structure members with default values.

		// Default values:
		//
		// Member                                   | Default value
		// -----------------------------------------|--------------------------------------
		// NumVertices                              | 0
		// Flags                                    | DRAW_FLAG_NONE
		// NumInstances                             | 1
		// StartVertexLocation                      | 0
		// FirstInstanceLocation                    | 0
		constexpr DrawAttribs() noexcept {}

		// Initializes the structure with user-specified values.
		constexpr DrawAttribs(
			uint32     _NumVertices,
			DRAW_FLAGS _Flags,
			uint32     _NumInstances = 1,
			uint32     _StartVertexLocation = 0,
			uint32     _FirstInstanceLocation = 0) noexcept
			: NumVertices(_NumVertices)
			, Flags(_Flags)
			, NumInstances(_NumInstances)
			, StartVertexLocation(_StartVertexLocation)
			, FirstInstanceLocation(_FirstInstanceLocation)
		{
		}
	};


	// Defines the indexed draw command attributes.

	// This structure is used by IDeviceContext::DrawIndexed().
	struct DrawIndexedAttribs
	{
		// The number of indices to draw.
		uint32     NumIndices = 0;

		// The type of elements in the index buffer.

		// Allowed values: `VT_UINT16` and `VT_UINT32`.
		VALUE_TYPE IndexType = VT_UNDEFINED;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// Number of instances to draw.

		// If more than one instance is specified, instanced draw call will be performed.
		uint32     NumInstances = 1;

		// LOCATION (NOT the byte offset) of the first index in
		// the index buffer to start reading indices from.
		uint32     FirstIndexLocation = 0;

		// A constant which is added to each index before accessing the vertex buffer.
		uint32     BaseVertex = 0;

		// LOCATION (or INDEX, but NOT the byte offset) in the vertex
		// buffer to start reading instance data from.
		uint32     FirstInstanceLocation = 0;


		// Initializes the structure members with default values.

		// Default values:
		// Member                                   | Default value
		// -----------------------------------------|--------------------------------------
		// NumIndices                               | 0
		// IndexType                                | VT_UNDEFINED
		// Flags                                    | DRAW_FLAG_NONE
		// NumInstances                             | 1
		// FirstIndexLocation                       | 0
		// BaseVertex                               | 0
		// FirstInstanceLocation                    | 0
		constexpr DrawIndexedAttribs() noexcept {}

		// Initializes the structure members with user-specified values.
		constexpr DrawIndexedAttribs(
			uint32      _NumIndices,
			VALUE_TYPE  _IndexType,
			DRAW_FLAGS  _Flags,
			uint32      _NumInstances = 1,
			uint32      _FirstIndexLocation = 0,
			uint32      _BaseVertex = 0,
			uint32      _FirstInstanceLocation = 0) noexcept
			: NumIndices(_NumIndices)
			, IndexType(_IndexType)
			, Flags(_Flags)
			, NumInstances(_NumInstances)
			, FirstIndexLocation(_FirstIndexLocation)
			, BaseVertex(_BaseVertex)
			, FirstInstanceLocation(_FirstInstanceLocation)
		{
		}
	};


	// Defines the indirect draw command attributes.

	// This structure is used by IDeviceContext::DrawIndirect().
	struct DrawIndirectAttribs
	{
		// A pointer to the buffer, from which indirect draw attributes will be read.

		// The buffer must contain the following arguments at the specified offset:
		//
		//     uint32 NumVertices;
		//     uint32 NumInstances;
		//     uint32 StartVertexLocation;
		//     uint32 FirstInstanceLocation;
		IBuffer* pAttribsBuffer = nullptr;

		// Offset from the beginning of the buffer to the location of draw command attributes.
		uint64 DrawArgsOffset = 0;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// The number of draw commands to execute. When the pCounterBuffer is not null, this member
		// defines the maximum number of commands that will be executed.
		// Must be less than DrawCommandProperties::MaxDrawIndirectCount.
		uint32 DrawCount = 1;

		// When `DrawCount > 1`, the byte stride between successive sets of draw parameters.
		// Must be a multiple of 4 and greater than or equal to 16 bytes (`sizeof(uint32) * 4`).
		uint32 DrawArgsStride = 16;

		// State transition mode for indirect draw arguments buffer.
		RESOURCE_STATE_TRANSITION_MODE  AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;


		// A pointer to the optional buffer, from which uint32 value with the draw count will be read.
		IBuffer* pCounterBuffer = nullptr;

		// When pCounterBuffer is not null, an offset from the beginning of the buffer to the
		// location of the command counter.
		uint64 CounterOffset = 0;

		// When counter buffer is not null, state transition mode for the count buffer.
		RESOURCE_STATE_TRANSITION_MODE  CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;


		// Initializes the structure members with default values
		constexpr DrawIndirectAttribs() noexcept {}

		// Initializes the structure members with user-specified values.
		explicit constexpr DrawIndirectAttribs(
			IBuffer* _pAttribsBuffer,
			DRAW_FLAGS _Flags,
			uint32 _DrawCount = DrawIndirectAttribs{}.DrawCount,
			uint64 _DrawArgsOffset = DrawIndirectAttribs{}.DrawArgsOffset,
			uint32 _DrawArgsStride = DrawIndirectAttribs{}.DrawArgsStride,
			RESOURCE_STATE_TRANSITION_MODE _AttribsBufferTransitionMode = DrawIndirectAttribs{}.AttribsBufferStateTransitionMode,
			IBuffer* _pCounterBuffer = DrawIndirectAttribs{}.pCounterBuffer,
			uint64 _CounterOffset = DrawIndirectAttribs{}.CounterOffset,
			RESOURCE_STATE_TRANSITION_MODE _CounterBufferTransitionMode = DrawIndirectAttribs{}.CounterBufferStateTransitionMode) noexcept
			: pAttribsBuffer(_pAttribsBuffer)
			, DrawArgsOffset(_DrawArgsOffset)
			, Flags(_Flags)
			, DrawCount(_DrawCount)
			, DrawArgsStride(_DrawArgsStride)
			, AttribsBufferStateTransitionMode(_AttribsBufferTransitionMode)
			, pCounterBuffer(_pCounterBuffer)
			, CounterOffset(_CounterOffset)
			, CounterBufferStateTransitionMode(_CounterBufferTransitionMode)
		{
		}
	};


	// Defines the indexed indirect draw command attributes.

	// This structure is used by IDeviceContext::DrawIndexedIndirect().
	struct DrawIndexedIndirectAttribs
	{
		// The type of the elements in the index buffer.

		// Allowed values: `VT_UINT16` and `VT_UINT32`.
		VALUE_TYPE IndexType = VT_UNDEFINED;

		// A pointer to the buffer, from which indirect draw attributes will be read.

		// The buffer must contain the following arguments at the specified offset:
		//
		//     uint32 NumIndices;
		//     uint32 NumInstances;
		//     uint32 FirstIndexLocation;
		//     uint32 BaseVertex;
		//     uint32 FirstInstanceLocation
		IBuffer* pAttribsBuffer = nullptr;

		// Offset from the beginning of the buffer to the location of the draw command attributes.
		uint64 DrawArgsOffset = 0;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// The number of draw commands to execute.

		// When the `pCounterBuffer` is not null, this member
		// defines the maximum number of commands that will be executed.
		// Must be less than DrawCommandProperties::MaxDrawIndirectCount.
		uint32 DrawCount = 1;

		// When `DrawCount > 1`, the byte stride between successive sets of draw parameters.

		// Must be a multiple of 4 and greater than or equal to 20 bytes (`sizeof(uint32) * 5`).
		uint32 DrawArgsStride = 20;

		// State transition mode for indirect draw arguments buffer.
		RESOURCE_STATE_TRANSITION_MODE AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;


		// A pointer to the optional buffer, from which uint32 value with the draw count will be read.
		IBuffer* pCounterBuffer = nullptr;

		// When `pCounterBuffer` is not null, offset from the beginning of the counter buffer to the
		// location of the command counter.
		uint64 CounterOffset = 0;

		// When counter buffer is not null, state transition mode for the count buffer.
		RESOURCE_STATE_TRANSITION_MODE CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;


		// Initializes the structure members with default values
		constexpr DrawIndexedIndirectAttribs() noexcept {}

		// Initializes the structure members with user-specified values.
		constexpr DrawIndexedIndirectAttribs(
			VALUE_TYPE _IndexType,
			IBuffer* _pAttribsBuffer,
			DRAW_FLAGS _Flags,
			uint32 _DrawCount = DrawIndexedIndirectAttribs{}.DrawCount,
			uint64 _DrawArgsOffset = DrawIndexedIndirectAttribs{}.DrawArgsOffset,
			uint32 _DrawArgsStride = DrawIndexedIndirectAttribs{}.DrawArgsStride,
			RESOURCE_STATE_TRANSITION_MODE _AttribsBufferTransitionMode = DrawIndexedIndirectAttribs{}.AttribsBufferStateTransitionMode,
			IBuffer* _pCounterBuffer = DrawIndexedIndirectAttribs{}.pCounterBuffer,
			uint64 _CounterOffset = DrawIndexedIndirectAttribs{}.CounterOffset,
			RESOURCE_STATE_TRANSITION_MODE _CounterBufferTransitionMode = DrawIndexedIndirectAttribs{}.CounterBufferStateTransitionMode) noexcept
			: IndexType(_IndexType)
			, pAttribsBuffer(_pAttribsBuffer)
			, DrawArgsOffset(_DrawArgsOffset)
			, Flags(_Flags)
			, DrawCount(_DrawCount)
			, DrawArgsStride(_DrawArgsStride)
			, AttribsBufferStateTransitionMode(_AttribsBufferTransitionMode)
			, pCounterBuffer(_pCounterBuffer)
			, CounterOffset(_CounterOffset)
			, CounterBufferStateTransitionMode(_CounterBufferTransitionMode)
		{
		}
	};


	// Defines the mesh draw command attributes.

	// This structure is used by IDeviceContext::DrawMesh().
	struct DrawMeshAttribs
	{
		// The number of groups dispatched in X direction.
		uint32 ThreadGroupCountX = 1;

		// The number of groups dispatched in Y direction.
		uint32 ThreadGroupCountY = 1;

		// The number of groups dispatched in Y direction.
		uint32 ThreadGroupCountZ = 1;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// Initializes the structure members with default values.
		constexpr DrawMeshAttribs() noexcept {}

		explicit constexpr DrawMeshAttribs(
			uint32     _ThreadGroupCountX,
			DRAW_FLAGS _Flags = DRAW_FLAG_NONE) noexcept
			: ThreadGroupCountX(_ThreadGroupCountX)
			, Flags(_Flags)
		{
		}

		constexpr DrawMeshAttribs(
			uint32     _ThreadGroupCountX,
			uint32     _ThreadGroupCountY,
			DRAW_FLAGS _Flags = DRAW_FLAG_NONE) noexcept
			: ThreadGroupCountX(_ThreadGroupCountX)
			, ThreadGroupCountY(_ThreadGroupCountY)
			, Flags(_Flags)
		{
		}

		constexpr DrawMeshAttribs(
			uint32     _ThreadGroupCountX,
			uint32     _ThreadGroupCountY,
			uint32     _ThreadGroupCountZ,
			DRAW_FLAGS _Flags = DRAW_FLAG_NONE) noexcept
			: ThreadGroupCountX(_ThreadGroupCountX)
			, ThreadGroupCountY(_ThreadGroupCountY)
			, ThreadGroupCountZ(_ThreadGroupCountZ)
			, Flags(_Flags)
		{
		}
	};


	// Defines the mesh indirect draw command attributes.

	// This structure is used by IDeviceContext::DrawMeshIndirect().
	struct DrawMeshIndirectAttribs
	{
		// A pointer to the buffer, from which indirect draw attributes will be read.
		// The buffer must contain the following arguments at the specified offset:
		//
		//   Direct3D12:
		//
		//        uint32 ThreadGroupCountX;
		//        uint32 ThreadGroupCountY;
		//        uint32 ThreadGroupCountZ;
		//
		//   Vulkan:
		//
		//        uint32 TaskCount;
		//        uint32 FirstTask;
		//
		// Size of the buffer must be `sizeof(uint32[3]) * Attribs.MaxDrawCommands`.
		IBuffer* pAttribsBuffer = nullptr;

		// Offset from the beginning of the attribs buffer to the location of the draw command attributes.
		uint64 DrawArgsOffset = 0;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// When `pCounterBuffer` is null, the number of commands to run.
		// When `pCounterBuffer` is not null, the maximum number of commands
		// that will be read from the count buffer.
		uint32 CommandCount = 1;

		// State transition mode for indirect draw arguments buffer.
		RESOURCE_STATE_TRANSITION_MODE AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;


		// A pointer to the optional buffer, from which uint32 value with the draw count will be read.
		IBuffer* pCounterBuffer = nullptr;

		// When pCounterBuffer is not null, an offset from the beginning of the buffer to the location of the command counter.
		uint64 CounterOffset = 0;

		// When pCounterBuffer is not null, state transition mode for the count buffer.
		RESOURCE_STATE_TRANSITION_MODE CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Initializes the structure members with default values
		constexpr DrawMeshIndirectAttribs() noexcept {}

		// Initializes the structure members with user-specified values.
		constexpr DrawMeshIndirectAttribs(
			IBuffer* _pAttribsBuffer,
			DRAW_FLAGS _Flags,
			uint32 _CommandCount,
			uint64 _DrawArgsOffset = DrawMeshIndirectAttribs{}.DrawArgsOffset,
			RESOURCE_STATE_TRANSITION_MODE _AttribsBufferStateTransitionMode = DrawMeshIndirectAttribs{}.AttribsBufferStateTransitionMode,
			IBuffer* _pCounterBuffer = DrawMeshIndirectAttribs{}.pCounterBuffer,
			uint64 _CounterOffset = DrawMeshIndirectAttribs{}.CounterOffset,
			RESOURCE_STATE_TRANSITION_MODE _CounterBufferStateTransitionMode = DrawMeshIndirectAttribs{}.CounterBufferStateTransitionMode) noexcept
			: pAttribsBuffer(_pAttribsBuffer)
			, DrawArgsOffset(_DrawArgsOffset)
			, Flags(_Flags)
			, CommandCount(_CommandCount)
			, AttribsBufferStateTransitionMode(_AttribsBufferStateTransitionMode)
			, pCounterBuffer(_pCounterBuffer)
			, CounterOffset(_CounterOffset)
			, CounterBufferStateTransitionMode(_CounterBufferStateTransitionMode)
		{
		}
	};


	// Multi-draw command item.
	struct MultiDrawItem
	{
		// The number of vertices to draw.
		uint32     NumVertices = 0;

		// LOCATION (or INDEX, but NOT the byte offset) of the first vertex in the
		// vertex buffer to start reading vertices from.
		uint32     StartVertexLocation = 0;
	};

	// MultiDraw command attributes.
	struct MultiDrawAttribs
	{
		// The number of draw items to execute.
		uint32               DrawCount = 0;

		// A pointer to the array of DrawCount draw command items.
		const MultiDrawItem* pDrawItems = nullptr;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// The number of instances to draw. If more than one instance is specified,
		// instanced draw call will be performed.
		uint32     NumInstances = 1;

		// LOCATION (or INDEX, but NOT the byte offset) in the vertex buffer to start
		// reading instance data from.
		uint32     FirstInstanceLocation = 0;

		constexpr MultiDrawAttribs() noexcept {}

		constexpr MultiDrawAttribs(
			uint32               _DrawCount,
			const MultiDrawItem* _pDrawItems,
			DRAW_FLAGS           _Flags,
			uint32               _NumInstances = 1,
			uint32               _FirstInstanceLocation = 0) noexcept
			: DrawCount(_DrawCount)
			, pDrawItems(_pDrawItems)
			, Flags(_Flags)
			, NumInstances(_NumInstances)
			, FirstInstanceLocation(_FirstInstanceLocation)
		{
		}
	};


	// Multi-draw indexed command item.
	struct MultiDrawIndexedItem
	{
		// The number of indices to draw.
		uint32 NumIndices = 0;

		// LOCATION (NOT the byte offset) of the first index in
		// the index buffer to start reading indices from.
		uint32 FirstIndexLocation = 0;

		// A constant which is added to each index before accessing the vertex buffer.
		uint32 BaseVertex = 0;
	};

	// MultiDraw command attributes.
	struct MultiDrawIndexedAttribs
	{
		// The number of draw items to execute.
		uint32 DrawCount = 0;

		// A pointer to the array of DrawCount draw command items.
		const MultiDrawIndexedItem* pDrawItems = nullptr;

		// The type of elements in the index buffer.

		// Allowed values: `VT_UINT16` and `VT_UINT32`.
		VALUE_TYPE IndexType = VT_UNDEFINED;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		// Number of instances to draw.

		// If more than one instance is specified, instanced draw call will be performed.
		uint32 NumInstances = 1;

		// LOCATION (or INDEX, but NOT the byte offset) in the vertex
		// buffer to start reading instance data from.
		uint32 FirstInstanceLocation = 0;

		constexpr MultiDrawIndexedAttribs() noexcept {}

		constexpr MultiDrawIndexedAttribs(
			uint32                      _DrawCount,
			const MultiDrawIndexedItem* _pDrawItems,
			VALUE_TYPE                  _IndexType,
			DRAW_FLAGS                  _Flags,
			uint32                      _NumInstances = 1,
			uint32                      _FirstInstanceLocation = 0) noexcept
			: DrawCount(_DrawCount)
			, pDrawItems(_pDrawItems)
			, IndexType(_IndexType)
			, Flags(_Flags)
			, NumInstances(_NumInstances)
			, FirstInstanceLocation(_FirstInstanceLocation)
		{
		}
	};


	// Defines which parts of the depth-stencil buffer to clear.

	// These flags are used by IDeviceContext::ClearDepthStencil().
	enum CLEAR_DEPTH_STENCIL_FLAGS : uint32
	{
		// Perform no clear.
		CLEAR_DEPTH_FLAG_NONE = 0x00,

		// Clear depth part of the buffer.
		CLEAR_DEPTH_FLAG = 0x01,

		// Clear stencil part of the buffer.
		CLEAR_STENCIL_FLAG = 0x02
	};
	DEFINE_FLAG_ENUM_OPERATORS(CLEAR_DEPTH_STENCIL_FLAGS);


	// Describes dispatch command arguments.

	// This structure is used by IDeviceContext::DispatchCompute().
	struct DispatchComputeAttribs
	{
		// The number of groups dispatched in X direction.
		uint32 ThreadGroupCountX = 1;

		// The number of groups dispatched in Y direction.
		uint32 ThreadGroupCountY = 1;

		// The number of groups dispatched in Z direction.
		uint32 ThreadGroupCountZ = 1;


		// Compute group X size.

		// \remarks
		//     This member is only used by Metal backend and is ignored by others.
		uint32 MtlThreadGroupSizeX = 0;

		// Compute group Y size.

		// \remarks
		//     This member is only used by Metal backend and is ignored by others.
		uint32 MtlThreadGroupSizeY = 0;

		// Compute group Z size.

		// \remarks
		//     This member is only used by Metal backend and is ignored by others.
		uint32 MtlThreadGroupSizeZ = 0;


		constexpr DispatchComputeAttribs() noexcept {}

		// Initializes the structure with user-specified values.
		constexpr DispatchComputeAttribs(uint32 GroupsX, uint32 GroupsY, uint32 GroupsZ = 1) noexcept
			: ThreadGroupCountX(GroupsX)
			, ThreadGroupCountY(GroupsY)
			, ThreadGroupCountZ(GroupsZ)
		{
		}
	};


	// Describes dispatch command arguments.

	// This structure is used by IDeviceContext::DispatchComputeIndirect().
	struct DispatchComputeIndirectAttribs
	{
		// A pointer to the buffer containing indirect dispatch attributes.

		// The buffer must contain the following arguments at the specified offset:
		//
		//     uint32 ThreadGroupCountX;
		//     uint32 ThreadGroupCountY;
		//     uint32 ThreadGroupCountZ;
		//
		IBuffer* pAttribsBuffer = nullptr;

		// State transition mode for indirect dispatch attributes buffer.
		RESOURCE_STATE_TRANSITION_MODE AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// The offset from the beginning of the buffer to the dispatch command arguments.
		uint64  DispatchArgsByteOffset = 0;


		// Compute group X size.

		// \remarks
		//     This member is only used by Metal backend and is ignored by others.
		uint32 MtlThreadGroupSizeX = 0;

		// Compute group Y size.

		// \remarks
		//     This member is only used by Metal backend and is ignored by others.
		uint32 MtlThreadGroupSizeY = 0;

		// Compute group Z size.

		// \remarks
		//     This member is only used by Metal backend and is ignored by others.
		uint32 MtlThreadGroupSizeZ = 0;


		constexpr DispatchComputeIndirectAttribs() noexcept {}

		// Initializes the structure with user-specified values.
		constexpr DispatchComputeIndirectAttribs(
			IBuffer* _pAttribsBuffer,
			RESOURCE_STATE_TRANSITION_MODE _StateTransitionMode,
			uint64 _Offset = 0)
			: pAttribsBuffer(_pAttribsBuffer)
			, AttribsBufferStateTransitionMode(_StateTransitionMode)
			, DispatchArgsByteOffset(_Offset)
		{
		}
	};


	// Describes tile dispatch command arguments.

	// This structure is used by IDeviceContext::DispatchTile().
	struct DispatchTileAttribs
	{
		// The number of threads in one tile dispatched in X direction.

		// Must not be greater than `TileSizeX` returned by IDeviceContext::GetTileSize().
		uint32 ThreadsPerTileX = 1;

		// The number of threads in one tile dispatched in Y direction.

		// Must not be greater than `TileSizeY` returned by IDeviceContext::GetTileSize().
		uint32 ThreadsPerTileY = 1;

		// Additional flags, see shz::DRAW_FLAGS.
		DRAW_FLAGS Flags = DRAW_FLAG_NONE;

		constexpr DispatchTileAttribs() noexcept {}

		// Initializes the structure with user-specified values.
		constexpr DispatchTileAttribs(
			uint32     _ThreadsX,
			uint32     _ThreadsY,
			DRAW_FLAGS _Flags = DRAW_FLAG_NONE) noexcept
			: ThreadsPerTileX(_ThreadsX)
			, ThreadsPerTileY(_ThreadsY)
			, Flags(_Flags)
		{
		}
	};


	// Describes multi-sampled texture resolve command arguments.

	// This structure is used by IDeviceContext::ResolveTextureSubresource().
	struct ResolveTextureSubresourceAttribs
	{
		// Mip level of the source multi-sampled texture to resolve.
		uint32 SrcMipLevel = 0;

		// Array slice of the source multi-sampled texture to resolve.
		uint32 SrcSlice = 0;

		// Source texture state transition mode, see shz::RESOURCE_STATE_TRANSITION_MODE.
		RESOURCE_STATE_TRANSITION_MODE SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Mip level of the destination non-multi-sampled texture.
		uint32 DstMipLevel = 0;

		// Array slice of the destination non-multi-sampled texture.
		uint32 DstSlice = 0;

		// Destination texture state transition mode, see shz::RESOURCE_STATE_TRANSITION_MODE.
		RESOURCE_STATE_TRANSITION_MODE DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// If one or both textures are typeless, specifies the type of the typeless texture.

		// If both texture formats are not typeless, in which case they must be identical, this member must be
		// either `TEX_FORMAT_UNKNOWN`, or match this format.
		TEXTURE_FORMAT Format = TEX_FORMAT_UNKNOWN;
	};


	// Defines allowed flags for IDeviceContext::SetVertexBuffers() function.
	enum SET_VERTEX_BUFFERS_FLAGS : uint8
	{
		// No extra operations.
		SET_VERTEX_BUFFERS_FLAG_NONE = 0x00,

		// Reset the vertex buffers to only the buffers specified in this
		// call. All buffers previously bound to the pipeline will be unbound.
		SET_VERTEX_BUFFERS_FLAG_RESET = 0x01
	};
	DEFINE_FLAG_ENUM_OPERATORS(SET_VERTEX_BUFFERS_FLAGS);


	// Describes the viewport.

	// This structure is used by IDeviceContext::SetViewports().
	struct Viewport
	{
		// X coordinate of the left boundary of the viewport.
		float32 TopLeftX = 0.f;

		// Y coordinate of the top boundary of the viewport.

		// When defining a viewport, DirectX convention is used:
		// window coordinate systems originates in the LEFT TOP corner
		// of the screen with Y axis pointing down.
		float32 TopLeftY = 0.f;

		// Viewport width.
		float32 Width = 0.f;

		// Viewport Height.
		float32 Height = 0.f;

		// Minimum depth of the viewport. Ranges between 0 and 1.
		float32 MinDepth = 0.f;

		// Maximum depth of the viewport. Ranges between 0 and 1.
		float32 MaxDepth = 1.f;

		// Initializes the structure.
		constexpr Viewport(
			float32 _TopLeftX,
			float32 _TopLeftY,
			float32 _Width,
			float32 _Height,
			float32 _MinDepth = 0,
			float32 _MaxDepth = 1) noexcept
			: TopLeftX(_TopLeftX)
			, TopLeftY(_TopLeftY)
			, Width(_Width)
			, Height(_Height)
			, MinDepth(_MinDepth)
			, MaxDepth(_MaxDepth)
		{
		}

		constexpr Viewport(
			uint32  _Width,
			uint32  _Height,
			float32 _MinDepth = 0,
			float32 _MaxDepth = 1) noexcept
			: Width(static_cast<float32>(_Width))
			, Height(static_cast<float32>(_Height))
			, MinDepth(_MinDepth)
			, MaxDepth(_MaxDepth)
		{
		}

		constexpr Viewport(float32 _Width, float32 _Height) noexcept
			: Width(_Width)
			, Height(_Height)
		{
		}

		explicit constexpr Viewport(const SwapChainDesc& SCDesc) noexcept
			: Viewport(SCDesc.Width, SCDesc.Height)
		{
		}

		constexpr bool operator == (const Viewport& vp) const
		{
			return TopLeftX == vp.TopLeftX &&
				TopLeftY == vp.TopLeftY &&
				Width == vp.Width &&
				Height == vp.Height &&
				MinDepth == vp.MinDepth &&
				MaxDepth == vp.MaxDepth;
		}

		constexpr bool operator != (const Viewport& vp) const
		{
			return !(*this == vp);
		}

		constexpr Viewport() noexcept {}
	};


	// Describes the rectangle.

	// This structure is used by IDeviceContext::SetScissorRects().
	//
	// \remarks When defining a viewport, Windows convention is used:
	//          window coordinate systems originates in the LEFT TOP corner
	//          of the screen with Y axis pointing down.
	struct Rect
	{
		int32 left = 0;  ///< X coordinate of the left boundary of the viewport.
		int32 top = 0;  ///< Y coordinate of the top boundary of the viewport.
		int32 right = 0;  ///< X coordinate of the right boundary of the viewport.
		int32 bottom = 0;  ///< Y coordinate of the bottom boundary of the viewport.

		// Initializes the structure
		constexpr Rect(int32 _left, int32 _top, int32 _right, int32 _bottom) noexcept
			: left(_left)
			, top(_top)
			, right(_right)
			, bottom(_bottom)
		{
		}

		constexpr Rect() noexcept {}

		constexpr bool IsValid() const
		{
			return right > left && bottom > top;
		}

		constexpr bool operator == (const Rect& rc) const
		{
			return left == rc.left &&
				top == rc.top &&
				right == rc.right &&
				bottom == rc.bottom;
		}

		constexpr bool operator != (const Rect& rc) const
		{
			return !(*this == rc);
		}
	};


	// Defines copy texture command attributes.

	// This structure is used by IDeviceContext::CopyTexture().
	struct CopyTextureAttribs
	{
		// Source texture to copy data from.
		ITexture* pSrcTexture = nullptr;

		// Mip level of the source texture to copy data from.
		uint32                         SrcMipLevel = 0;

		// Array slice of the source texture to copy data from. Must be 0 for non-array textures.
		uint32                         SrcSlice = 0;

		// Source region to copy. Use nullptr to copy the entire subresource.
		const Box* pSrcBox = nullptr;

		// Source texture state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Destination texture.
		ITexture* pDstTexture = nullptr;

		// Destination mip level.
		uint32 DstMipLevel = 0;

		// Destination array slice. Must be 0 for non-array textures.
		uint32 DstSlice = 0;

		// X offset on the destination subresource.
		uint32 DstX = 0;

		// Y offset on the destination subresource.
		uint32 DstY = 0;

		// Z offset on the destination subresource
		uint32 DstZ = 0;

		// Destination texture state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;


		constexpr CopyTextureAttribs() noexcept {}

		constexpr CopyTextureAttribs(
			ITexture* _pSrcTexture,
			RESOURCE_STATE_TRANSITION_MODE _SrcTextureTransitionMode,
			ITexture* _pDstTexture,
			RESOURCE_STATE_TRANSITION_MODE _DstTextureTransitionMode) noexcept
			: pSrcTexture(_pSrcTexture)
			, SrcTextureTransitionMode(_SrcTextureTransitionMode)
			, pDstTexture(_pDstTexture)
			, DstTextureTransitionMode(_DstTextureTransitionMode)
		{
		}
	};


	// SetRenderTargetsExt command attributes.

	// This structure is used by IDeviceContext::SetRenderTargetsExt().
	struct SetRenderTargetsAttribs
	{
		// Number of render targets to bind.
		uint32 NumRenderTargets = 0;

		// Array of pointers to ITextureView that represent the render
		// targets to bind to the device. The type of each view in the
		// array must be shz::TEXTURE_VIEW_RENDER_TARGET.
		ITextureView** ppRenderTargets = nullptr;

		// Pointer to the ITextureView that represents the depth stencil to
		// bind to the device. The view type must be
		// shz::TEXTURE_VIEW_DEPTH_STENCIL or shz::TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL.
		ITextureView* pDepthStencil = nullptr;

		// Shading rate texture view. Set null to disable variable rate shading.
		ITextureView* pShadingRateMap = nullptr;

		// State transition mode of the render targets, depth stencil buffer
		// and shading rate map being set (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		constexpr SetRenderTargetsAttribs() noexcept {}

		constexpr SetRenderTargetsAttribs(
			uint32 _NumRenderTargets,
			ITextureView* _ppRenderTargets[],
			ITextureView* _pDepthStencil = nullptr,
			RESOURCE_STATE_TRANSITION_MODE _StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE,
			ITextureView* _pShadingRateMap = nullptr) noexcept
			: NumRenderTargets(_NumRenderTargets)
			, ppRenderTargets(_ppRenderTargets)
			, pDepthStencil(_pDepthStencil)
			, pShadingRateMap(_pShadingRateMap)
			, StateTransitionMode(_StateTransitionMode)
		{
		}
	};


	// BeginRenderPass command attributes.

	// This structure is used by IDeviceContext::BeginRenderPass().
	struct BeginRenderPassAttribs
	{
		// Render pass to begin.
		IRenderPass* pRenderPass = nullptr;

		// Framebuffer containing the attachments that are used with the render pass.
		IFramebuffer* pFramebuffer = nullptr;

		// The number of elements in pClearValues array.
		uint32 ClearValueCount = 0;

		// Clear values for the attachments.

		// A pointer to an array of `ClearValueCount` OptimizedClearValue structures that contains
		// clear values for each attachment, if the attachment uses a `LoadOp` value of shz::ATTACHMENT_LOAD_OP_CLEAR
		// or if the attachment has a depth/stencil format and uses a `StencilLoadOp` value of shz::ATTACHMENT_LOAD_OP_CLEAR.
		// The array is indexed by attachment number. Only elements corresponding to cleared attachments are used.
		// Other elements of pClearValues are ignored.
		OptimizedClearValue* pClearValues = nullptr;

		// Framebuffer attachments state transition mode before the render pass begins.

		// This parameter also indicates how attachment states should be handled when
		// transitioning between subpasses as well as after the render pass ends.
		// When shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION is used, attachment states will be
		// updated so that they match the state in the current subpass as well as the final states
		// specified by the render pass when the pass ends.
		// Note that resources are always transitioned. The flag only indicates if the internal
		// state variables should be updated.
		// When shz::RESOURCE_STATE_TRANSITION_MODE_NONE or shz::RESOURCE_STATE_TRANSITION_MODE_VERIFY is used,
		// internal state variables are not updated and it is the application responsibility to set them
		// manually to match the actual states.
		RESOURCE_STATE_TRANSITION_MODE StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;
	};


	// TLAS instance flags that are used in IDeviceContext::BuildTLAS().
	enum RAYTRACING_INSTANCE_FLAGS : uint8
	{
		// No flags are set.
		RAYTRACING_INSTANCE_NONE = 0,

		// Disables face culling for this instance.
		RAYTRACING_INSTANCE_TRIANGLE_FACING_CULL_DISABLE = 0x01,

		// Indicates that the front face of the triangle for culling purposes is the face that is counter
		// clockwise in object space relative to the ray origin. Because the facing is determined in object
		// space, an instance transform matrix does not change the winding, but a geometry transform does.
		RAYTRACING_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE = 0x02,

		// Causes this instance to act as though shz::RAYTRACING_GEOMETRY_FLAGS_OPAQUE were specified on all
		// geometries referenced by this instance. This behavior can be overridden in the shader with ray flags.
		RAYTRACING_INSTANCE_FORCE_OPAQUE = 0x04,

		// Causes this instance to act as though shz::RAYTRACING_GEOMETRY_FLAGS_OPAQUE were not specified on all
		// geometries referenced by this instance. This behavior can be overridden in the shader with ray flags.
		RAYTRACING_INSTANCE_FORCE_NO_OPAQUE = 0x08,

		RAYTRACING_INSTANCE_FLAG_LAST = RAYTRACING_INSTANCE_FORCE_NO_OPAQUE
	};
	DEFINE_FLAG_ENUM_OPERATORS(RAYTRACING_INSTANCE_FLAGS);


	// Defines acceleration structure copy mode.

	// These the flags used by IDeviceContext::CopyBLAS() and IDeviceContext::CopyTLAS().
	enum COPY_AS_MODE : uint8
	{
		// Creates a direct copy of the acceleration structure specified in pSrc into the one specified by pDst.
		// The pDst acceleration structure must have been created with the same parameters as pSrc.
		COPY_AS_MODE_CLONE = 0,

		// Creates a more compact version of an acceleration structure pSrc into pDst.
		// The acceleration structure pDst must have been created with a `CompactedSize` corresponding
		// to the one returned by IDeviceContext::WriteBLASCompactedSize() or IDeviceContext::WriteTLASCompactedSize()
		// after the build of the acceleration structure specified by pSrc.
		COPY_AS_MODE_COMPACT,

		COPY_AS_MODE_LAST = COPY_AS_MODE_COMPACT,
	};


	// Defines geometry flags for ray tracing.
	enum RAYTRACING_GEOMETRY_FLAGS : uint8
	{
		// No flags are set.
		RAYTRACING_GEOMETRY_FLAG_NONE = 0,

		// Indicates that this geometry does not invoke the any-hit shaders even if present in a hit group.
		RAYTRACING_GEOMETRY_FLAG_OPAQUE = 0x01,

		// Indicates that the implementation must only call the any-hit shader a single time for each primitive in this geometry.
		// If this bit is absent an implementation may invoke the any-hit shader more than once for this geometry.
		RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANY_HIT_INVOCATION = 0x02,

		RAYTRACING_GEOMETRY_FLAG_LAST = RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANY_HIT_INVOCATION
	};
	DEFINE_FLAG_ENUM_OPERATORS(RAYTRACING_GEOMETRY_FLAGS);


	// Triangle geometry data description.
	struct BLASBuildTriangleData
	{
		// Geometry name used to map a geometry to a hit group in the shader binding table.

		// Add geometry data to the geometry that is allocated by BLASTriangleDesc with the same name.
		const Char* GeometryName = nullptr;

		// Triangle vertices data source.

		// Triangles are considered "inactive" if the x component of each vertex is NaN.
		// The buffer must be created with BIND_RAY_TRACING flag.
		IBuffer* pVertexBuffer = nullptr;

		// Data offset, in bytes, in `pVertexBuffer`.

		// * D3D12 and Vulkan: offset must be a multiple of the `VertexValueType` size.
		// * Metal:            stride must be aligned by RayTracingProperties::VertexBufferAlignment
		//                     and must be a multiple of the `VertexStride`.
		uint64 VertexOffset = 0;

		// Stride, in bytes, between vertices.

		// * D3D12 and Vulkan: stride must be a multiple of the `VertexValueType` size.
		// * Metal:            stride must be aligned by RayTracingProperties::VertexBufferAlignment.
		uint32 VertexStride = 0;

		// The number of triangle vertices.

		// Must be less than or equal to BLASTriangleDesc::MaxVertexCount.
		uint32 VertexCount = 0;

		// The type of the vertex components.

		// This is an optional value. Must be undefined or same as in BLASTriangleDesc.
		VALUE_TYPE VertexValueType = VT_UNDEFINED;

		// The number of vertex components.

		// This is an optional value. Must be undefined or same as in BLASTriangleDesc.
		uint8 VertexComponentCount = 0;

		// The number of triangles.

		// Must equal to `VertexCount / 3` if `pIndexBuffer` is `null` or must be equal to index count / 3.
		uint32 PrimitiveCount = 0;

		// Triangle indices data source.

		// Must be null if BLASTriangleDesc::IndexType is undefined.
		// The buffer must be created with shz::BIND_RAY_TRACING flag.
		IBuffer* pIndexBuffer = nullptr;

		// Data offset in bytes in pIndexBuffer.

		// Offset must be aligned by RayTracingProperties::IndexBufferAlignment
		// and must be a multiple of the IndexType size.
		uint64 IndexOffset = 0;

		// The type of triangle indices, see shz::VALUE_TYPE.

		// This is an optional value. Must be undefined or same as in BLASTriangleDesc.
		VALUE_TYPE IndexType = VT_UNDEFINED;

		// Geometry transformation data source, must contain a `float4x3` matrix aka shz::InstanceMatrix.

		// The buffer must be created with shz::BIND_RAY_TRACING flag.
		// \note Transform buffer is not supported in Metal backend.
		IBuffer* pTransformBuffer = nullptr;

		// Data offset in bytes in `pTransformBuffer`.

		// Offset must be aligned by RayTracingProperties::TransformBufferAlignment.
		uint64 TransformBufferOffset = 0;

		// Geometry flags, se shz::RAYTRACING_GEOMETRY_FLAGS.
		RAYTRACING_GEOMETRY_FLAGS Flags = RAYTRACING_GEOMETRY_FLAG_NONE;
	};


	// AABB geometry data description.
	struct BLASBuildBoundingBoxData
	{
		// Geometry name used to map geometry to hit group in shader binding table.

		// Put geometry data to geometry that allocated by BLASBoundingBoxDesc with the same name.
		const Char* GeometryName = nullptr;

		// AABB data source.

		// Each AABB is defined as `{ float3 Min; float3 Max }` structure.
		//
		// An AABB are considered inactive if `AABB.Min.x` is `NaN`.
		//
		// The buffer must be created with shz::BIND_RAY_TRACING flag.
		IBuffer* pBoxBuffer = nullptr;

		// Data offset in bytes in pBoxBuffer.

		// * D3D12 and Vulkan: offset must be aligned by RayTracingProperties::BoxBufferAlignment.
		// * Metal:            offset must be aligned by RayTracingProperties::BoxBufferAlignment
		//                    and must be a multiple of the BoxStride.
		uint64 BoxOffset = 0;

		// Stride in bytes between each AABB.

		// Stride must be aligned by RayTracingProperties::BoxBufferAlignment.
		uint32 BoxStride = 0;

		// Number of AABBs.

		// Must be less than or equal to BLASBoundingBoxDesc::MaxBoxCount.
		uint32 BoxCount = 0;

		// Geometry flags, see shz::RAYTRACING_GEOMETRY_FLAGS.
		RAYTRACING_GEOMETRY_FLAGS Flags = RAYTRACING_GEOMETRY_FLAG_NONE;
	};


	// This structure is used by IDeviceContext::BuildBLAS().
	struct BuildBLASAttribs
	{
		// Target bottom-level AS.

		// \note
		//     Access to the BLAS must be externally synchronized.
		IBottomLevelAS* pBLAS = nullptr;

		// Bottom-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE  BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Geometry data source buffers state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE  GeometryTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// A pointer to the array of `TriangleDataCount` `BLASBuildTriangleData` structures that contains triangle geometry data.

		// If `Update` is `true`:
		//   - Only vertex positions (in `pVertexBuffer`) and transformation (in `pTransformBuffer`) can be changed.
		//   - All other content in BLASBuildTriangleData and buffers must be the same as what was used to build BLAS.
		//   - To disable geometry, make all triangles inactive, see BLASBuildTriangleData::pVertexBuffer description.
		BLASBuildTriangleData const* pTriangleData = nullptr;

		// The number of triangle geometries.

		// Must be less than or equal to BottomLevelASDesc::TriangleCount.
		// If `Update` is `true` then the count must be the same as the one used to build BLAS.
		uint32                          TriangleDataCount = 0;

		// A pointer to an array of BoxDataCount BLASBuildBoundingBoxData structures that contain AABB geometry data.

		// If `Update` is `true`:
		//   - AABB coordinates (in `pBoxBuffer`) can be changed.
		//   - All other content in BLASBuildBoundingBoxData must be same as used to build BLAS.
		//   - To disable geometry make all AAABBs inactive, see BLASBuildBoundingBoxData::pBoxBuffer description.
		BLASBuildBoundingBoxData const* pBoxData = nullptr;

		// The number of AABB geometries.

		// Must be less than or equal to BottomLevelASDesc::BoxCount.
		// If `Update` is `true` then the count must be the same as the one used to build BLAS.
		uint32 BoxDataCount = 0;

		// The buffer that is used for acceleration structure building.

		// Must be created with shz::BIND_RAY_TRACING.
		// Call IBottomLevelAS::GetScratchBufferSizes().Build to get the minimal size for the scratch buffer.
		IBuffer* pScratchBuffer = nullptr;

		// Offset from the beginning of the buffer.

		// Offset must be aligned by RayTracingProperties::ScratchBufferAlignment.
		uint64 ScratchBufferOffset = 0;

		// Scratch buffer state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE  ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Whether to build the acceleration structure from scratch or update it.

		// if `false` then BLAS will be built from scratch.
		// If `true` then previous content of BLAS will be updated.
		//
		// `pBLAS` must be created with shz::RAYTRACING_BUILD_AS_ALLOW_UPDATE flag.
		//
		// An update will be faster than building an acceleration structure from scratch.
		bool Update = false;
	};


	// Can be used to calculate the TLASBuildInstanceData::ContributionToHitGroupIndex depending on instance count,
	// geometry count in each instance (in TLASBuildInstanceData::pBLAS) and shader binding mode in BuildTLASAttribs::BindingMode.
	//
	// Example:
	//  InstanceOffset = BaseContributionToHitGroupIndex;
	//  For each instance in TLAS
	//     if (Instance.ContributionToHitGroupIndex == TLAS_INSTANCE_OFFSET_AUTO)
	//         Instance.ContributionToHitGroupIndex = InstanceOffset;
	//         if (BindingMode == HIT_GROUP_BINDING_MODE_PER_GEOMETRY) InstanceOffset += Instance.pBLAS->GeometryCount() * HitGroupStride;
	//         if (BindingMode == HIT_GROUP_BINDING_MODE_PER_INSTANCE) InstanceOffset += HitGroupStride;

	static constexpr uint32 TLAS_INSTANCE_OFFSET_AUTO = 0xFFFFFFFFU;


	// Row-major 4x3 matrix
	struct InstanceMatrix
	{
		// Matrix data.

		// The matrix is stored in row-major order:
		//
		//            rotation        translation
		//     ([0,0]  [0,1]  [0,2])   ([0,3])
		//     ([1,0]  [1,1]  [1,2])   ([1,3])
		//     ([2,0]  [2,1]  [2,2])   ([2,3])
		float data[3][4];

		// Construct identity matrix.
		constexpr InstanceMatrix() noexcept
			: data{ { 1.0f, 0.0f, 0.0f, 0.0f },
					{ 0.0f, 1.0f, 0.0f, 0.0f },
					{ 0.0f, 0.0f, 1.0f, 0.0f } }
		{
		}

		constexpr InstanceMatrix(const InstanceMatrix&)  noexcept = default;
		constexpr InstanceMatrix(InstanceMatrix&&) noexcept = default;
		constexpr InstanceMatrix& operator=(const InstanceMatrix&)  noexcept = default;
		constexpr InstanceMatrix& operator=(InstanceMatrix&&) noexcept = default;

		// Sets the translation part.
		InstanceMatrix& SetTranslation(float x, float y, float z) noexcept
		{
			data[0][3] = x;
			data[1][3] = y;
			data[2][3] = z;
			return *this;
		}

		// Sets the rotation part.
		InstanceMatrix& SetRotation(const float* pMatrix, uint32 RowSize = 3) noexcept
		{
			for (uint32 r = 0; r < 3; ++r)
			{
				for (uint32 c = 0; c < 3; ++c)
					data[r][c] = pMatrix[c * RowSize + r];
			}

			return *this;
		}
	};


	// This structure is used by BuildTLASAttribs.
	struct TLASBuildInstanceData
	{
		// Instance name that is used to map an instance to a hit group in shader binding table.
		const Char* InstanceName = nullptr;

		// Bottom-level AS that represents instance geometry.

		// Once built, TLAS will hold strong reference to pBLAS until next build or copy operation.
		//
		// \note
		//     Access to the BLAS must be externally synchronized.
		IBottomLevelAS* pBLAS = nullptr;

		// Instance to world transformation.
		InstanceMatrix Transform;

		// User-defined value that can be accessed in the shader

		//
		// * HLSL: `InstanceID()` in closest-hit and intersection shaders.
		// * HLSL: `RayQuery::CommittedInstanceID()` within inline ray tracing.
		// * GLSL: `gl_InstanceCustomIndex` in closest-hit and intersection shaders.
		// * GLSL: `rayQueryGetIntersectionInstanceCustomIndex` within inline ray tracing.
		// * MSL:  `intersection_result< instancing >::instance_id`.
		//
		// Only the lower 24 bits are used.
		uint32 CustomId = 0;

		// Instance flags, see shz::RAYTRACING_INSTANCE_FLAGS.
		RAYTRACING_INSTANCE_FLAGS Flags = RAYTRACING_INSTANCE_NONE;

		// Visibility mask for the geometry, the instance may only be hit if `rayMask & instance.Mask != 0`.

		// * `rayMask` in GLSL is a `cullMask` argument of `traceRay()`
		// * `rayMask` in HLSL is an `InstanceInclusionMask` argument of `TraceRay()`.
		uint8 Mask = 0xFF;

		// The index used to calculate the hit group location in the shader binding table.

		// Must be shz::TLAS_INSTANCE_OFFSET_AUTO if BuildTLASAttribs::BindingMode is not shz::SHADER_BINDING_USER_DEFINED.
		// Only the lower 24 bits are used.
		uint32 ContributionToHitGroupIndex = TLAS_INSTANCE_OFFSET_AUTO;
	};


	// Top-level AS instance size in bytes in GPU side.
	// Used to calculate size of BuildTLASAttribs::pInstanceBuffer.
	static constexpr uint32 TLAS_INSTANCE_DATA_SIZE = 64;


	// This structure is used by IDeviceContext::BuildTLAS().
	struct BuildTLASAttribs
	{
		// Target top-level AS.

		// Access to the TLAS must be externally synchronized.
		ITopLevelAS* pTLAS = nullptr;

		// Top-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE  TLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Bottom-level AS (in TLASBuildInstanceData::pBLAS) state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE  BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// A pointer to an array of `InstanceCount` `TLASBuildInstanceData` structures that contain instance data.

		// If `Update` is `true`:
		//     - Any instance data can be changed.
		//     - To disable an instance set TLASBuildInstanceData::Mask to zero or set empty TLASBuildInstanceData::BLAS to pBLAS.
		TLASBuildInstanceData const* pInstances = nullptr;

		// The number of instances.

		// Must be less than or equal to TopLevelASDesc::MaxInstanceCount.
		// If Update is true then count must be the same as used to build TLAS.
		uint32 InstanceCount = 0;

		// The buffer that will be used to store instance data during AS building.

		// The buffer size must be at least `TLAS_INSTANCE_DATA_SIZE * InstanceCount`.
		// The buffer must be created with shz::BIND_RAY_TRACING flag.
		IBuffer* pInstanceBuffer = nullptr;

		// Offset from the beginning of the buffer to the location of instance data.
		// Offset must be aligned by RayTracingProperties::InstanceBufferAlignment.
		uint64 InstanceBufferOffset = 0;

		// Instance buffer state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE  InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// The number of hit shaders that can be bound for a single geometry or an instance (depends on BindingMode).

		//   - Used to calculate TLASBuildInstanceData::ContributionToHitGroupIndex.
		//   - Ignored if `BindingMode` is shz::SHADER_BINDING_USER_DEFINED.
		//
		// You should use the same value in a shader:
		// * `MultiplierForGeometryContributionToHitGroupIndex` argument in `TraceRay()` in HLSL
		// * `sbtRecordStride` argument in `traceRay()` in GLSL.
		uint32 HitGroupStride = 1;

		// Base offset for the hit group location.

		// Can be used to bind hit shaders for multiple acceleration structures, see IShaderBindingTable::BindHitGroupForGeometry().
		//   - Used to calculate TLASBuildInstanceData::ContributionToHitGroupIndex.
		//   - Ignored if `BindingMode` is shz::SHADER_BINDING_USER_DEFINED.
		uint32 BaseContributionToHitGroupIndex = 0;

		// Hit shader binding mode, see shz::SHADER_BINDING_MODE.

		// Used to calculate TLASBuildInstanceData::ContributionToHitGroupIndex.
		HIT_GROUP_BINDING_MODE BindingMode = HIT_GROUP_BINDING_MODE_PER_GEOMETRY;

		// Buffer that is used for acceleration structure building.

		// Must be created with shz::BIND_RAY_TRACING.
		//
		// Call ITopLevelAS::GetScratchBufferSizes().Build to get the minimal size for the scratch buffer.
		//
		// \note
		//     Access to the TLAS must be externally synchronized.
		IBuffer* pScratchBuffer = nullptr;

		// Offset from the beginning of the buffer.

		// Offset must be aligned by RayTracingProperties::ScratchBufferAlignment.
		uint64                          ScratchBufferOffset = 0;

		// Scratch buffer state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE  ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Whether to build the acceleration structure from scratch or update it.

		// * if `false`, then TLAS will be built from scratch.
		// * If `true`, then previous content of TLAS will be updated.
		//
		// pTLAS must be created with shz::RAYTRACING_BUILD_AS_ALLOW_UPDATE flag.
		//
		// \note
		//     An update will be faster than building an acceleration structure from scratch.
		bool Update = false;
	};


	// This structure is used by IDeviceContext::CopyBLAS().
	struct CopyBLASAttribs
	{
		// Source bottom-level AS.

		// \note
		//     Access to the BLAS must be externally synchronized.
		IBottomLevelAS* pSrc = nullptr;

		// Destination bottom-level AS.

		// If `Mode` is shz::COPY_AS_MODE_COMPACT then `pDst` must be created with `CompactedSize`
		// that is greater or equal to the size returned by IDeviceContext::WriteBLASCompactedSize.
		//
		// \note
		//     Access to the BLAS must be externally synchronized.
		IBottomLevelAS* pDst = nullptr;

		// Acceleration structure copy mode, see shz::COPY_AS_MODE.
		COPY_AS_MODE Mode = COPY_AS_MODE_CLONE;

		// Source bottom-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE SrcTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Destination bottom-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE DstTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		constexpr CopyBLASAttribs() noexcept {}

		constexpr CopyBLASAttribs(
			IBottomLevelAS* _pSrc,
			IBottomLevelAS* _pDst,
			COPY_AS_MODE                   _Mode = CopyBLASAttribs{}.Mode,
			RESOURCE_STATE_TRANSITION_MODE _SrcTransitionMode = CopyBLASAttribs{}.SrcTransitionMode,
			RESOURCE_STATE_TRANSITION_MODE _DstTransitionMode = CopyBLASAttribs{}.DstTransitionMode) noexcept
			: pSrc(_pSrc)
			, pDst(_pDst)
			, Mode(_Mode)
			, SrcTransitionMode(_SrcTransitionMode)
			, DstTransitionMode(_DstTransitionMode)
		{
		}
	};


	// This structure is used by IDeviceContext::CopyTLAS().
	struct CopyTLASAttribs
	{
		// Source top-level AS.

		// \note
		//     Access to the TLAS must be externally synchronized.
		ITopLevelAS* pSrc = nullptr;

		// Destination top-level AS.

		// If `Mode` is shz::COPY_AS_MODE_COMPACT then `pDst` must be created with `CompactedSize`
		// that is greater or equal to size that returned by IDeviceContext::WriteTLASCompactedSize.
		//
		// \note
		//     Access to the TLAS must be externally synchronized.
		ITopLevelAS* pDst = nullptr;

		// Acceleration structure copy mode, see shz::COPY_AS_MODE.
		COPY_AS_MODE Mode = COPY_AS_MODE_CLONE;

		// Source top-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE SrcTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Destination top-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE DstTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		constexpr CopyTLASAttribs() noexcept {}

		constexpr CopyTLASAttribs(
			ITopLevelAS* _pSrc,
			ITopLevelAS* _pDst,
			COPY_AS_MODE                   _Mode = CopyTLASAttribs{}.Mode,
			RESOURCE_STATE_TRANSITION_MODE _SrcTransitionMode = CopyTLASAttribs{}.SrcTransitionMode,
			RESOURCE_STATE_TRANSITION_MODE _DstTransitionMode = CopyTLASAttribs{}.DstTransitionMode) noexcept
			: pSrc(_pSrc)
			, pDst(_pDst)
			, Mode(_Mode)
			, SrcTransitionMode(_SrcTransitionMode)
			, DstTransitionMode(_DstTransitionMode)
		{
		}
	};


	// This structure is used by IDeviceContext::WriteBLASCompactedSize().
	struct WriteBLASCompactedSizeAttribs
	{
		// Bottom-level AS.
		IBottomLevelAS* pBLAS = nullptr;

		// The destination buffer into which a 64-bit value representing the acceleration structure compacted size will be written to.

		// \remarks  Metal backend writes a 32-bit value.
		IBuffer* pDestBuffer = nullptr;

		// Offset from the beginning of the buffer to the location of the AS compacted size.
		uint64 DestBufferOffset = 0;

		// Bottom-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Destination buffer state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE BufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		constexpr WriteBLASCompactedSizeAttribs() noexcept {}

		constexpr WriteBLASCompactedSizeAttribs(
			IBottomLevelAS* _pBLAS,
			IBuffer* _pDestBuffer,
			uint64                         _DestBufferOffset = WriteBLASCompactedSizeAttribs{}.DestBufferOffset,
			RESOURCE_STATE_TRANSITION_MODE _BLASTransitionMode = WriteBLASCompactedSizeAttribs{}.BLASTransitionMode,
			RESOURCE_STATE_TRANSITION_MODE _BufferTransitionMode = WriteBLASCompactedSizeAttribs{}.BufferTransitionMode) noexcept
			: pBLAS(_pBLAS)
			, pDestBuffer(_pDestBuffer)
			, DestBufferOffset(_DestBufferOffset)
			, BLASTransitionMode(_BLASTransitionMode)
			, BufferTransitionMode(_BufferTransitionMode)
		{
		}
	};


	// This structure is used by IDeviceContext::WriteTLASCompactedSize().
	struct WriteTLASCompactedSizeAttribs
	{
		// Top-level AS.
		ITopLevelAS* pTLAS = nullptr;

		// The destination buffer into which a 64-bit value representing the acceleration structure compacted size will be written to.

		// \remarks  Metal backend writes a 32-bit value.
		IBuffer* pDestBuffer = nullptr;

		// Offset from the beginning of the buffer to the location of the AS compacted size.
		uint64 DestBufferOffset = 0;

		// Top-level AS state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE TLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// Destination buffer state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE BufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		constexpr WriteTLASCompactedSizeAttribs() noexcept {}

		constexpr WriteTLASCompactedSizeAttribs(ITopLevelAS* _pTLAS,
			IBuffer* _pDestBuffer,
			uint64                         _DestBufferOffset = WriteTLASCompactedSizeAttribs{}.DestBufferOffset,
			RESOURCE_STATE_TRANSITION_MODE _TLASTransitionMode = WriteTLASCompactedSizeAttribs{}.TLASTransitionMode,
			RESOURCE_STATE_TRANSITION_MODE _BufferTransitionMode = WriteTLASCompactedSizeAttribs{}.BufferTransitionMode) noexcept
			: pTLAS(_pTLAS)
			, pDestBuffer(_pDestBuffer)
			, DestBufferOffset(_DestBufferOffset)
			, TLASTransitionMode(_TLASTransitionMode)
			, BufferTransitionMode(_BufferTransitionMode)
		{
		}
	};


	// This structure is used by IDeviceContext::TraceRays().
	struct TraceRaysAttribs
	{
		// Shader binding table.
		const IShaderBindingTable* pSBT = nullptr;

		uint32 DimensionX = 1; ///< The number of rays dispatched in X direction.
		uint32 DimensionY = 1; ///< The number of rays dispatched in Y direction.
		uint32 DimensionZ = 1; ///< The number of rays dispatched in Z direction.

		constexpr TraceRaysAttribs() noexcept {}

		constexpr TraceRaysAttribs(
			const IShaderBindingTable* _pSBT,
			uint32 _DimensionX,
			uint32 _DimensionY,
			uint32 _DimensionZ = TraceRaysAttribs{}.DimensionZ) noexcept
			: pSBT(_pSBT)
			, DimensionX(_DimensionX)
			, DimensionY(_DimensionY)
			, DimensionZ(_DimensionZ)
		{
		}
	};


	// This structure is used by IDeviceContext::TraceRaysIndirect().
	struct TraceRaysIndirectAttribs
	{
		// Shader binding table.
		const IShaderBindingTable* pSBT = nullptr;

		// A pointer to the buffer containing indirect trace rays attributes.

		// The buffer must contain the following arguments at the specified offset:
		//
		//     [88 bytes reserved] - for Direct3D12 backend
		//     uint32 DimensionX;
		//     uint32 DimensionY;
		//     uint32 DimensionZ;
		//
		// \remarks  Use IDeviceContext::UpdateSBT() to initialize the first 88 bytes with the
		//           same shader binding table as specified in TraceRaysIndirectAttribs::pSBT.
		IBuffer* pAttribsBuffer = nullptr;

		// State transition mode for indirect trace rays attributes buffer.
		RESOURCE_STATE_TRANSITION_MODE AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		// The offset from the beginning of the buffer to the trace rays command arguments.
		uint64  ArgsByteOffset = 0;

		constexpr TraceRaysIndirectAttribs() noexcept {}

		constexpr TraceRaysIndirectAttribs(
			const IShaderBindingTable* _pSBT,
			IBuffer* _pAttribsBuffer,
			RESOURCE_STATE_TRANSITION_MODE _TransitionMode = TraceRaysIndirectAttribs{}.AttribsBufferStateTransitionMode,
			uint64                         _ArgsByteOffset = TraceRaysIndirectAttribs{}.ArgsByteOffset) noexcept
			: pSBT(_pSBT)
			, pAttribsBuffer(_pAttribsBuffer)
			, AttribsBufferStateTransitionMode(_TransitionMode)
			, ArgsByteOffset(_ArgsByteOffset)
		{
		}
	};


	// This structure is used by IDeviceContext::UpdateSBT().
	struct UpdateIndirectRTBufferAttribs
	{
		// Indirect buffer that can be used by IDeviceContext::TraceRaysIndirect() command.
		IBuffer* pAttribsBuffer = nullptr;

		// Offset in bytes from the beginning of the buffer where SBT data will be recorded.
		uint64 AttribsBufferOffset = 0;

		// State transition mode of the attribs buffer (see shz::RESOURCE_STATE_TRANSITION_MODE).
		RESOURCE_STATE_TRANSITION_MODE TransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;

		constexpr UpdateIndirectRTBufferAttribs() noexcept {}

		explicit constexpr UpdateIndirectRTBufferAttribs(
			IBuffer* _pAttribsBuffer,
			uint64                         _AttribsBufferOffset = UpdateIndirectRTBufferAttribs{}.AttribsBufferOffset,
			RESOURCE_STATE_TRANSITION_MODE _TransitionMode = UpdateIndirectRTBufferAttribs{}.TransitionMode) noexcept
			: pAttribsBuffer(_pAttribsBuffer)
			, AttribsBufferOffset(_AttribsBufferOffset)
			, TransitionMode(_TransitionMode)
		{
		}
	};


	// Defines the sparse buffer memory binding range.

	// This structure is used by SparseBufferMemoryBindInfo.
	struct SparseBufferMemoryBindRange
	{
		// Offset in buffer address space where memory will be bound/unbound.

		// Must be a multiple of the SparseBufferProperties::BlockSize.
		uint64 BufferOffset = 0;

		// Memory range offset in pMemory.

		// Must be a multiple of the SparseBufferProperties::BlockSize.
		uint64 MemoryOffset = 0;

		// Size of the memory which will be bound/unbound.

		// Must be a multiple of the SparseBufferProperties::BlockSize.
		uint64 MemorySize = 0;

		// Pointer to the memory object.

		// If non-null, the memory will be bound to the Region; otherwise the memory will be unbound.
		//
		// \remarks
		//     * Direct3D11: the entire buffer must use a single memory object. When a resource is bound to a new memory object,
		//                   all previous bindings are invalidated.
		//     * Vulkan & Direct3D12: different resource regions may be bound to different memory objects.
		//     * Vulkan: memory object must be compatible with the resource, use IDeviceMemory::IsCompatible() to ensure that.
		//
		// \note  Memory object can be created by the engine using IRenderDevice::CreateDeviceMemory() or can be implemented by the user.
		//        Memory object must implement interface methods for each backend (IDeviceMemoryD3D11, IDeviceMemoryD3D12, IDeviceMemoryVk).
		IDeviceMemory* pMemory = nullptr;

		constexpr SparseBufferMemoryBindRange() noexcept {}

		constexpr SparseBufferMemoryBindRange(
			uint64 _BufferOffset,
			uint64 _MemoryOffset,
			uint64 _MemorySize,
			IDeviceMemory* _pMemory) noexcept
			: BufferOffset(_BufferOffset)
			, MemoryOffset(_MemoryOffset)
			, MemorySize(_MemorySize)
			, pMemory(_pMemory)
		{
		}
	};

	// Defines the sparse buffer memory binding information.

	// This structure is used by BindSparseResourceMemoryAttribs.
	struct SparseBufferMemoryBindInfo
	{
		// Buffer for which sparse binding command will be executed.
		IBuffer* pBuffer = nullptr;

		// An array of `NumRanges` buffer memory ranges to bind/unbind,
		// see shz::SparseBufferMemoryBindRange.
		const SparseBufferMemoryBindRange* pRanges = nullptr;

		// The number of elements in `pRanges` array.
		uint32 NumRanges = 0;
	};

	// Defines the sparse texture memory binding range.

	// This structure is used by SparseTextureMemoryBind.
	struct SparseTextureMemoryBindRange
	{
		// Mip level that contains the region to bind.

		// \note If this level is equal to SparseTextureProperties::FirstMipInTail,
		//       all subsequent mip levels will also be affected.
		uint32 MipLevel = 0;

		// Texture array slice index.
		uint32 ArraySlice = 0;

		// Region in pixels where to bind/unbind memory.

		// Must be a multiple of SparseTextureProperties::TileSize.
		//
		// If `MipLevel` is equal to SparseTextureProperties::FirstMipInTail, this field is ignored
		// and `OffsetInMipTail` is used instead.
		//
		// If `Region` contains multiple tiles, they are bound in the row-major order.
		Box Region = {};

		// Offset in mip tail in bytes.

		// When mip tail consists of multiple memory blocks, this member
		// defines the starting offset to bind/unbind memory in the tail.
		// If `MipLevel` is less than SparseTextureProperties::FirstMipInTail,
		// this field is ignored and `Region` is used.
		uint64 OffsetInMipTail = 0;

		// Size of the memory that will be bound/unbound to this Region.

		// Memory size must be equal to the number of tiles in Region multiplied by the
		// sparse memory block size.
		// It must be a multiple of the SparseTextureProperties::BlockSize.
		// 
		// \remarks Ignored in Metal.
		uint64 MemorySize = 0;

		// Memory range offset in the `pMemory`.

		// Must be a multiple of the SparseTextureProperties::BlockSize.
		//
		// \remarks Ignored in Metal.
		uint64 MemoryOffset = 0;

		// Pointer to the memory object.

		// If non-null, the memory will be bound to Region; otherwise the memory will be unbound.
		//
		// \remarks
		//     * Direct3D11: the entire texture must use a single memory object; when a resource is bound to a new memory
		//                   object, all previous bindings are invalidated.
		//     * Vulkan & Direct3D12: different resource regions may be bound to different memory objects.
		//     * Metal: must be the same memory object that was used to create the sparse texture,
		//              see IRenderDeviceMtl::CreateSparseTexture().
		//     * Vulkan: memory object must be compatible with the resource, use IDeviceMemory::IsCompatible() to ensure that.
		//
		// \note
		//     * Memory object can be created by the engine using IRenderDevice::CreateDeviceMemory() or can be implemented by the user.
		//     * Memory object must implement interface methods for each backend (IDeviceMemoryD3D11, IDeviceMemoryD3D12, IDeviceMemoryVk).
		IDeviceMemory* pMemory = nullptr;
	};

	// Sparse texture memory binding information.

	// This structure is used by BindSparseResourceMemoryAttribs.
	struct SparseTextureMemoryBindInfo
	{
		// Texture for which sparse binding command will be executed.
		ITexture* pTexture = nullptr;

		// An array of NumRanges texture memory ranges to bind/unbind, see shz::SparseTextureMemoryBindRange.
		const SparseTextureMemoryBindRange* pRanges = nullptr;

		// The number of elements in the pRanges array.
		uint32 NumRanges = 0;
	};

	// Attributes of the IDeviceContext::BindSparseResourceMemory() command.
	struct BindSparseResourceMemoryAttribs
	{
		// An array of `NumBufferBinds` sparse buffer bind commands.

		// All commands must bind/unbind unique range in the buffer.
		// Not supported in Metal.
		const SparseBufferMemoryBindInfo* pBufferBinds = nullptr;

		// The number of elements in the `pBufferBinds` array.
		uint32 NumBufferBinds = 0;

		// An array of `NumTextureBinds` sparse texture bind commands.

		// All commands must bind/unbind unique region in the texture.
		const SparseTextureMemoryBindInfo* pTextureBinds = nullptr;

		// The number of elements in the `pTextureBinds`.
		uint32 NumTextureBinds = 0;

		// An array of `NumWaitFences` fences to wait.

		// \remarks The context will wait until all fences have reached the values
		//          specified in `pWaitFenceValues`.
		IFence** ppWaitFences = nullptr;

		// An array of `NumWaitFences` values that the context should wait for the fences to reach.
		const uint64* pWaitFenceValues = nullptr;

		// The number of elements in the `ppWaitFences` and `pWaitFenceValues` arrays.
		uint32 NumWaitFences = 0;

		// An array of `NumSignalFences` fences to signal.
		IFence** ppSignalFences = nullptr;

		// An array of `NumSignalFences` values to set the fences to.
		const uint64* pSignalFenceValues = nullptr;

		// The number of elements in the `ppSignalFences` and `pSignalFenceValues` arrays.
		uint32 NumSignalFences = 0;
	};

	// Special constant for all remaining mipmap levels.
	static constexpr uint32 REMAINING_MIP_LEVELS = 0xFFFFFFFFU;
	// Special constant for all remaining array slices.
	static constexpr uint32 REMAINING_ARRAY_SLICES = 0xFFFFFFFFU;

	// Resource state transition flags.
	enum STATE_TRANSITION_FLAGS : uint8
	{
		// No flags.
		STATE_TRANSITION_FLAG_NONE = 0,

		// Indicates that the internal resource state should be updated to the new state
		// specified by StateTransitionDesc, and the engine should take over the resource state
		// management. If an application was managing the resource state manually, it is
		// responsible for making sure that all subresources are indeed in the designated state.
		// If not used, internal resource state will be unchanged.
		//
		// \note This flag cannot be used when StateTransitionDesc.TransitionType is shz::STATE_TRANSITION_TYPE_BEGIN.
		STATE_TRANSITION_FLAG_UPDATE_STATE = 1u << 0,

		// If set, the contents of the resource will be discarded, when possible.
		// This may avoid potentially expensive operations such as render target decompression
		// or a pipeline stall when transitioning to COMMON or UAV state.
		STATE_TRANSITION_FLAG_DISCARD_CONTENT = 1u << 1,

		// Indicates state transition between aliased resources that share the same memory.
		// Currently it is only supported for sparse resources that were created with aliasing flag.
		STATE_TRANSITION_FLAG_ALIASING = 1u << 2
	};
	DEFINE_FLAG_ENUM_OPERATORS(STATE_TRANSITION_FLAGS);


	// Resource state transition barrier description
	struct StateTransitionDesc
	{
		// Previous resource for aliasing transition.

		// This member is only used for aliasing transition (shz::STATE_TRANSITION_FLAG_ALIASING flag is set),
		// and ignored otherwise, and must point to a texture or a buffer object.
		//
		// \note `pResourceBefore` may be `null`, which indicates that any sparse or
		//       normal resource could cause aliasing.
		IDeviceObject* pResourceBefore = nullptr;

		// Resource to transition.

		// Can be ITexture, IBuffer, IBottomLevelAS, ITopLevelAS.
		//
		// \note For aliasing transition (shz::STATE_TRANSITION_FLAG_ALIASING flag is set),
		//       `pResource` may be `null`, which indicates that any sparse or
		//       normal resource could cause aliasing.
		IDeviceObject* pResource = nullptr;

		// When transitioning a texture, first mip level of the subresource range to transition.
		uint32 FirstMipLevel = 0;

		// When transitioning a texture, number of mip levels of the subresource range to transition.
		uint32 MipLevelsCount = REMAINING_MIP_LEVELS;

		// When transitioning a texture, first array slice of the subresource range to transition.
		uint32 FirstArraySlice = 0;

		// When transitioning a texture, number of array slices of the subresource range to transition.
		uint32 ArraySliceCount = REMAINING_ARRAY_SLICES;

		// Resource state before transition.

		// If this value is shz::RESOURCE_STATE_UNKNOWN, internal resource state will be used,
		// which must be defined in this case.
		//
		// \note  Resource state must be compatible with the context type.
		RESOURCE_STATE OldState = RESOURCE_STATE_UNKNOWN;

		// Resource state after transition.

		// Must **not** be shz::RESOURCE_STATE_UNKNOWN or shz::RESOURCE_STATE_UNDEFINED.
		//
		// \note  Resource state must be compatible with the context type.
		RESOURCE_STATE NewState = RESOURCE_STATE_UNKNOWN;

		// State transition type, see shz::STATE_TRANSITION_TYPE.

		// \note When issuing UAV barrier (i.e. `OldState` and `NewState` equal shz::RESOURCE_STATE_UNORDERED_ACCESS),
		//       `TransitionType` must be shz::STATE_TRANSITION_TYPE_IMMEDIATE.
		STATE_TRANSITION_TYPE TransitionType = STATE_TRANSITION_TYPE_IMMEDIATE;

		// State transition flags, see shz::STATE_TRANSITION_FLAGS.
		STATE_TRANSITION_FLAGS Flags = STATE_TRANSITION_FLAG_NONE;

		constexpr  StateTransitionDesc() noexcept {}

		constexpr StateTransitionDesc(
			ITexture* _pTexture,
			RESOURCE_STATE _OldState,
			RESOURCE_STATE _NewState,
			uint32 _FirstMipLevel = 0,
			uint32 _MipLevelsCount = REMAINING_MIP_LEVELS,
			uint32 _FirstArraySlice = 0,
			uint32 _ArraySliceCount = REMAINING_ARRAY_SLICES,
			STATE_TRANSITION_TYPE  _TransitionType = STATE_TRANSITION_TYPE_IMMEDIATE,
			STATE_TRANSITION_FLAGS _Flags = STATE_TRANSITION_FLAG_NONE) noexcept
			: pResource(static_cast<IDeviceObject*>(_pTexture))
			, FirstMipLevel(_FirstMipLevel)
			, MipLevelsCount(_MipLevelsCount)
			, FirstArraySlice(_FirstArraySlice)
			, ArraySliceCount(_ArraySliceCount)
			, OldState(_OldState)
			, NewState(_NewState)
			, TransitionType(_TransitionType)
			, Flags(_Flags)
		{
		}

		constexpr StateTransitionDesc(
			ITexture* _pTexture,
			RESOURCE_STATE _OldState,
			RESOURCE_STATE _NewState,
			STATE_TRANSITION_FLAGS _Flags) noexcept
			: StateTransitionDesc(
				_pTexture,
				_OldState,
				_NewState,
				0,
				REMAINING_MIP_LEVELS,
				0,
				REMAINING_ARRAY_SLICES,
				STATE_TRANSITION_TYPE_IMMEDIATE,
				_Flags)
		{
		}

		constexpr StateTransitionDesc(
			IBuffer* _pBuffer,
			RESOURCE_STATE _OldState,
			RESOURCE_STATE _NewState,
			STATE_TRANSITION_FLAGS _Flags = STATE_TRANSITION_FLAG_NONE) noexcept
			: pResource(_pBuffer)
			, OldState(_OldState)
			, NewState(_NewState)
			, Flags(_Flags)
		{
		}

		constexpr StateTransitionDesc(
			IBottomLevelAS* _pBLAS,
			RESOURCE_STATE _OldState,
			RESOURCE_STATE _NewState,
			STATE_TRANSITION_FLAGS _Flags = STATE_TRANSITION_FLAG_NONE) noexcept
			: pResource(_pBLAS)
			, OldState(_OldState)
			, NewState(_NewState)
			, Flags(_Flags)
		{
		}

		constexpr StateTransitionDesc(
			ITopLevelAS* _pTLAS,
			RESOURCE_STATE _OldState,
			RESOURCE_STATE _NewState,
			STATE_TRANSITION_FLAGS _Flags = STATE_TRANSITION_FLAG_NONE) noexcept
			: pResource(_pTLAS)
			, OldState(_OldState)
			, NewState(_NewState)
			, Flags(_Flags)
		{
		}

		// Aliasing barrier
		constexpr StateTransitionDesc(IDeviceObject* _pResourceBefore, IDeviceObject* _pResourceAfter) noexcept
			: pResourceBefore(_pResourceBefore)
			, pResource(_pResourceAfter)
			, Flags(STATE_TRANSITION_FLAG_ALIASING)
		{
		}
	};

	// Device context command counters.
	struct DeviceContextCommandCounters
	{
		// The total number of SetPipelineState calls.
		uint32 SetPipelineState = 0;

		// The total number of CommitShaderResources calls.
		uint32 CommitShaderResources = 0;

		// The total number of SetVertexBuffers calls.
		uint32 SetVertexBuffers = 0;

		// The total number of SetIndexBuffer calls.
		uint32 SetIndexBuffer = 0;

		// The total number of SetRenderTargets calls.
		uint32 SetRenderTargets = 0;

		// The total number of SetBlendFactors calls.
		uint32 SetBlendFactors = 0;

		// The total number of SetStencilRef calls.
		uint32 SetStencilRef = 0;

		// The total number of SetViewports calls.
		uint32 SetViewports = 0;

		// The total number of SetScissorRects calls.
		uint32 SetScissorRects = 0;

		// The total number of ClearRenderTarget calls.
		uint32 ClearRenderTarget = 0;

		// The total number of ClearDepthStencil calls.
		uint32 ClearDepthStencil = 0;

		// The total number of Draw calls.
		uint32 Draw = 0;

		// The total number of DrawIndexed calls.
		uint32 DrawIndexed = 0;

		// The total number of indirect DrawIndirect calls.
		uint32 DrawIndirect = 0;

		// The total number of indexed indirect DrawIndexedIndirect calls.
		uint32 DrawIndexedIndirect = 0;

		// The total number of MultiDraw calls.
		uint32 MultiDraw = 0;

		// The total number of MultiDrawIndexed calls.
		uint32 MultiDrawIndexed = 0;

		// The total number of DispatchCompute calls.
		uint32 DispatchCompute = 0;

		// The total number of DispatchComputeIndirect calls.
		uint32 DispatchComputeIndirect = 0;

		// The total number of DispatchTile calls.
		uint32 DispatchTile = 0;

		// The total number of DrawMesh calls.
		uint32 DrawMesh = 0;

		// The total number of DrawMeshIndirect calls.
		uint32 DrawMeshIndirect = 0;

		// The total number of BuildBLAS calls.
		uint32 BuildBLAS = 0;

		// The total number of BuildTLAS calls.
		uint32 BuildTLAS = 0;

		// The total number of CopyBLAS calls.
		uint32 CopyBLAS = 0;

		// The total number of CopyTLAS calls.
		uint32 CopyTLAS = 0;

		// The total number of WriteBLASCompactedSize calls.
		uint32 WriteBLASCompactedSize = 0;

		// The total number of WriteTLASCompactedSize calls.
		uint32 WriteTLASCompactedSize = 0;

		// The total number of TraceRays calls.
		uint32 TraceRays = 0;

		// The total number of TraceRaysIndirect calls.
		uint32 TraceRaysIndirect = 0;

		// The total number of UpdateSBT calls.
		uint32 UpdateSBT = 0;

		// The total number of UpdateBuffer calls.
		uint32 UpdateBuffer = 0;

		// The total number of CopyBuffer calls.
		uint32 CopyBuffer = 0;

		// The total number of MapBuffer calls.
		uint32 MapBuffer = 0;

		// The total number of UpdateTexture calls.
		uint32 UpdateTexture = 0;

		// The total number of CopyTexture calls.
		uint32 CopyTexture = 0;

		// The total number of MapTextureSubresource calls.
		uint32 MapTextureSubresource = 0;

		// The total number of BeginQuery calls.
		uint32 BeginQuery = 0;

		// The total number of GenerateMips calls.
		uint32 GenerateMips = 0;

		// The total number of ResolveTextureSubresource calls.
		uint32 ResolveTextureSubresource = 0;

		// The total number of BindSparseResourceMemory calls.
		uint32 BindSparseResourceMemory = 0;
	};

	// Device context statistics.
	struct DeviceContextStats
	{
		// The total number of primitives rendered, for each primitive topology.
		uint32 PrimitiveCounts[PRIMITIVE_TOPOLOGY_NUM_TOPOLOGIES] = {};

		// Command counters, see shz::DeviceContextCommandCounters.
		DeviceContextCommandCounters CommandCounters = {};

		constexpr uint32 GetTotalTriangleCount() const noexcept
		{
			return PrimitiveCounts[PRIMITIVE_TOPOLOGY_TRIANGLE_LIST] +
				PrimitiveCounts[PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP] +
				PrimitiveCounts[PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_ADJ];
		}

		constexpr uint32 GetTotalLineCount() const noexcept
		{
			return PrimitiveCounts[PRIMITIVE_TOPOLOGY_LINE_LIST] +
				PrimitiveCounts[PRIMITIVE_TOPOLOGY_LINE_STRIP] +
				PrimitiveCounts[PRIMITIVE_TOPOLOGY_LINE_STRIP_ADJ];
		}

		constexpr uint32 GetTotalPointCount() const noexcept
		{
			return PrimitiveCounts[PRIMITIVE_TOPOLOGY_POINT_LIST];
		}
	};





	// Device context interface.

	// \remarks Device context keeps strong references to all objects currently bound to
	//          the pipeline: buffers, states, samplers, shaders, etc.
	//          The context also keeps strong reference to the device and
	//          the swap chain.
	struct SHZ_INTERFACE IDeviceContext : public IObject
	{
		// Returns the context description
		virtual const DeviceContextDesc& GetDesc() const = 0;

		// Begins recording commands in the deferred context.

		// This method must be called before any command in the deferred context may be recorded.
		//
		// \param [in] ImmediateContextId - the ID of the immediate context where commands from this
		//                                  deferred context will be executed,
		//                                  see shz::DeviceContextDesc::ContextId.
		//
		// \warning Command list recorded by the context must not be submitted to any other immediate context
		//          other than one identified by ImmediateContextId.
		virtual void Begin(uint32 ImmediateContextId) = 0;

		// Sets the pipeline state.

		// \param [in] pPipelineState - Pointer to IPipelineState interface to bind to the context.
		//
		// \remarks
		//     * Supported contexts for graphics and mesh pipeline:        graphics.
		//     * Supported contexts for compute and ray tracing pipeline:  graphics and compute.
		virtual void SetPipelineState(IPipelineState* pPipelineState) = 0;


		// Transitions shader resources to the states required by Draw or Dispatch command.
		//
		// \param [in] pShaderResourceBinding - Shader resource binding whose resources will be transitioned.
		//
		// This method explicitly transitions all resources except ones in unknown state to the states required
		// by Draw or Dispatch command.
		// If this method was called, there is no need to use shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION
		// when calling CommitShaderResources()
		//
		// \remarks Resource state transitioning is **not thread-safe**. As the method may alter the states
		//          of resources referenced by the shader resource binding, no other thread is allowed to read or
		//          write these states.\n
		//          If the application intends to use the same resources in other threads simultaneously, it needs to
		//          explicitly manage the states using TransitionResourceStates() method.
		virtual void TransitionShaderResources(IShaderResourceBinding* pShaderResourceBinding) = 0;

		// Commits shader resources to the device context.

		// \param [in] pShaderResourceBinding - Shader resource binding whose resources will be committed.
		//                                      If pipeline state contains no shader resources, this parameter
		//                                      can be null.
		// \param [in] StateTransitionMode    - State transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE).
		//
		// If shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION mode is used,
		// the engine will also transition all shader resources to required states. If the flag
		// is not set, it is assumed that all resources are already in correct states.\n
		// Resources can be explicitly transitioned to required states by calling
		// TransitionShaderResources() or TransitionResourceStates().
		//
		// \remarks Automatic resource state transitioning is not thread-safe.
		//
		// - If shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION mode is used, the method may alter the states
		//   of resources referenced by the shader resource binding and no other thread is allowed to read or write these states.
		//
		// - If shz::RESOURCE_STATE_TRANSITION_MODE_VERIFY mode is used, the method will read the states, so no other thread
		//   should alter the states by calling any of the methods that use shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION mode.
		//   It is safe for other threads to read the states.
		//
		// - If shz::RESOURCE_STATE_TRANSITION_MODE_NONE mode is used, the method does not access the states of resources.
		//
		// If the application intends to use the same resources in other threads simultaneously, it should manage the states
		// manually by setting the state to shz::RESOURCE_STATE_UNKNOWN (which will disable automatic state
		// management) using IBuffer::SetState() or ITexture::SetState() and explicitly transitioning the states with
		// TransitionResourceStates().
		//
		// If an application calls any method that changes the state of any resource after it has been committed, the
		// application is responsible for transitioning the resource back to correct state using one of the available methods
		// before issuing the next draw or dispatch command.
		virtual void CommitShaderResources(IShaderResourceBinding* pShaderResourceBinding, RESOURCE_STATE_TRANSITION_MODE StateTransitionMode) = 0;

		// Sets the stencil reference value.

		// \param [in] StencilRef - Stencil reference value.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetStencilRef(uint32 StencilRef) = 0;


		// Sets the blend factors for alpha blending.

		// \param [in] pBlendFactors - Array of four blend factors, one for each RGBA component.
		//                             These factors are used if the blend state uses one of the
		//                             shz::BLEND_FACTOR_BLEND_FACTOR or
		//                             shz::BLEND_FACTOR_INV_BLEND_FACTOR
		//                             blend factors. If `nullptr` is provided,
		//                             default blend factors array `{1, 1, 1, 1}` will be used.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetBlendFactors(const float* pBlendFactors = nullptr) = 0;


		// Binds vertex buffers to the pipeline.

		// \param [in] StartSlot           - The first input slot for binding. The first vertex buffer is
		//                                   explicitly bound to the start slot; each additional vertex buffer
		//                                   in the array is implicitly bound to each subsequent input slot.
		// \param [in] NumBuffersSet       - The number of vertex buffers in the array.
		// \param [in] ppBuffers           - A pointer to an array of vertex buffers.
		//                                   The buffers must have been created with the shz::BIND_VERTEX_BUFFER flag.
		// \param [in] pOffsets            - Pointer to an array of offset values; one offset value for each buffer
		//                                   in the vertex-buffer array. Each offset is the number of bytes between
		//                                   the first element of a vertex buffer and the first element that will be
		//                                   used. If this parameter is nullptr, zero offsets for all buffers will be used.
		// \param [in] StateTransitionMode - State transition mode for buffers being set (see shz::RESOURCE_STATE_TRANSITION_MODE).
		// \param [in] Flags               - Additional flags. See shz::SET_VERTEX_BUFFERS_FLAGS for a list of allowed values.
		//
		// The device context keeps strong references to all bound vertex buffers.
		// Thus a buffer cannot be released until it is unbound from the context.\n
		// It is suggested to specify shz::SET_VERTEX_BUFFERS_FLAG_RESET flag
		// whenever possible. This will assure that no buffers from previous draw calls
		// are bound to the pipeline.
		//
		// When StateTransitionMode is shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, the method will
		// transition all buffers in known states to shz::RESOURCE_STATE_VERTEX_BUFFER. Resource state
		// transitioning is **not thread-safe**, so no other thread is allowed to read or write the states of
		// these buffers.
		//
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetVertexBuffers(
			uint32 StartSlot,
			uint32 NumBuffersSet,
			IBuffer* const* ppBuffers,
			const uint64* pOffsets,
			RESOURCE_STATE_TRANSITION_MODE StateTransitionMode,
			SET_VERTEX_BUFFERS_FLAGS Flags = SET_VERTEX_BUFFERS_FLAG_NONE) = 0;


		// Invalidates the cached context state.

		// This method should be called by an application to invalidate
		// internal cached states.
		virtual void InvalidateState() = 0;


		// Binds an index buffer to the pipeline.

		// \param [in] pIndexBuffer        - Pointer to the index buffer. The buffer must have been created
		//                                   with the shz::BIND_INDEX_BUFFER flag.
		// \param [in] ByteOffset          - Offset from the beginning of the buffer to
		//                                   the start of index data.
		// \param [in] StateTransitionMode - State transition mode for the index buffer to bind (see shz::RESOURCE_STATE_TRANSITION_MODE).
		//
		// \remarks The device context keeps strong reference to the index buffer.
		//          Thus an index buffer object cannot be released until it is unbound
		//          from the context.
		//
		// When StateTransitionMode is shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, the method will
		// transition the buffer to shz::RESOURCE_STATE_INDEX_BUFFER (if its state is not unknown). Resource
		// state transitioning is **not thread-safe**, so no other thread is allowed to read or write the state of
		// the buffer.
		//
		// If the application intends to use the same resource in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetIndexBuffer(
			IBuffer* pIndexBuffer,
			uint64 ByteOffset,
			RESOURCE_STATE_TRANSITION_MODE StateTransitionMode) = 0;


		// Sets an array of viewports.

		// \param [in] NumViewports - Number of viewports to set.
		// \param [in] pViewports   - An array of Viewport structures describing the viewports to bind.
		// \param [in] RTWidth      - Render target width. If 0 is provided, width of the currently bound render target will be used.
		// \param [in] RTHeight     - Render target height. If 0 is provided, height of the currently bound render target will be used.
		//
		// DirectX and OpenGL use different window coordinate systems. In DirectX, the coordinate system origin
		// is in the left top corner of the screen with Y axis pointing down. In OpenGL, the origin
		// is in the left bottom corner of the screen with Y axis pointing up. Render target size is
		// required to convert viewport from DirectX to OpenGL coordinate system if OpenGL device is used.\n\n
		// All viewports must be set atomically as one operation. Any viewports not
		// defined by the call are disabled.\n\n
		// You can set the viewport size to match the currently bound render target using the
		// following call:
		//
		//     pContext->SetViewports(1, nullptr, 0, 0);
		//
		// \remarks Supported contexts: graphics.
		virtual void SetViewports(
			uint32 NumViewports,
			const Viewport* pViewports,
			uint32 RTWidth,
			uint32 RTHeight) = 0;


		// Sets active scissor rects.

		// \param [in] NumRects - Number of scissor rectangles to set.
		// \param [in] pRects   - An array of Rect structures describing the scissor rectangles to bind.
		// \param [in] RTWidth  - Render target width. If 0 is provided, width of the currently bound render target will be used.
		// \param [in] RTHeight - Render target height. If 0 is provided, height of the currently bound render target will be used.
		//
		// \remarks
		//     DirectX and OpenGL use different window coordinate systems. In DirectX, the coordinate system origin
		//     is in the left top corner of the screen with Y axis pointing down. In OpenGL, the origin
		//     is in the left bottom corner of the screen with Y axis pointing up. Render target size is
		//     required to convert viewport from DirectX to OpenGL coordinate system if OpenGL device is used.\n\n
		//
		// All scissor rects must be set atomically as one operation. Any rects not
		// defined by the call are disabled.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetScissorRects(
			uint32 NumRects,
			const Rect* pRects,
			uint32 RTWidth,
			uint32 RTHeight) = 0;


		// Binds one or more render targets and the depth-stencil buffer to the context. It also
		// sets the viewport to match the first non-null render target or depth-stencil buffer.

		// \param [in] NumRenderTargets    - Number of render targets to bind.
		// \param [in] ppRenderTargets     - Array of pointers to ITextureView that represent the render
		//                                   targets to bind to the device. The type of each view in the
		//                                   array must be shz::TEXTURE_VIEW_RENDER_TARGET.
		// \param [in] pDepthStencil       - Pointer to the ITextureView that represents the depth stencil to
		//                                   bind to the device. The view type must be
		//                                   shz::TEXTURE_VIEW_DEPTH_STENCIL or shz::TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL.
		// \param [in] StateTransitionMode - State transition mode of the render targets and depth stencil buffer being set (see shz::RESOURCE_STATE_TRANSITION_MODE).
		//
		// The device context will keep strong references to all bound render target
		// and depth-stencil views. Thus these views (and consequently referenced textures)
		// cannot be released until they are unbound from the context.
		//
		// Any render targets not defined by this call are set to `nullptr`.
		//
		// When `StateTransitionMode` is shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, the method will
		// transition all render targets in known states to shz::RESOURCE_STATE_RENDER_TARGET,
		// and the depth-stencil buffer to shz::RESOURCE_STATE_DEPTH_WRITE state.
		// Resource state transitioning is **not thread-safe**, so no other thread is allowed to read or write
		// the states of resources used by the command.
		//
		// If the application intends to use the same resource in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetRenderTargets(
			uint32 NumRenderTargets,
			ITextureView* ppRenderTargets[],
			ITextureView* pDepthStencil,
			RESOURCE_STATE_TRANSITION_MODE StateTransitionMode) = 0;


		// Binds one or more render targets, the depth-stencil buffer and shading rate map to the context.
		// It also sets the viewport to match the first non-null render target or depth-stencil buffer.

		// \param [in] Attribs - The command attributes, see shz::SetRenderTargetsAttribs for details.
		//
		// The device context will keep strong references to all bound render target
		// and depth-stencil views as well as to shading rate map. Thus these views
		// (and consequently referenced textures) cannot be released until they are
		// unbound from the context.
		//
		// Any render targets not defined by this call are set to nullptr.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetRenderTargetsExt(const SetRenderTargetsAttribs& Attribs) = 0;


		// Begins a new render pass.

		// \param [in] Attribs - The command attributes, see shz::BeginRenderPassAttribs for details.
		//
		// \remarks Supported contexts: graphics.
		virtual void BeginRenderPass(const BeginRenderPassAttribs& Attribs) = 0;


		// Transitions to the next subpass in the render pass instance.

		// \remarks Supported contexts: graphics.
		virtual void NextSubpass() = 0;


		// Ends current render pass.

		// \remarks Supported contexts: graphics.
		virtual void EndRenderPass() = 0;


		// Executes a draw command.

		// \param [in] Attribs - Draw command attributes, see shz::DrawAttribs for details.
		//
		// If shz::DRAW_FLAG_VERIFY_STATES flag is set, the method reads the state of vertex
		// buffers, so no other threads are allowed to alter the states of the same resources.
		// It is OK to read these states.
		//
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks Supported contexts: graphics.
		virtual void Draw(const DrawAttribs& Attribs) = 0;


		// Executes an indexed draw command.

		// \param [in] Attribs - Draw command attributes, see shz::DrawIndexedAttribs for details.
		//
		// If shz::DRAW_FLAG_VERIFY_STATES flag is set, the method reads the state of vertex/index
		// buffers, so no other threads are allowed to alter the states of the same resources.
		// It is OK to read these states.
		//
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks Supported contexts: graphics.
		virtual void DrawIndexed(const DrawIndexedAttribs& Attribs) = 0;


		// Executes an indirect draw command.

		// \param [in] Attribs - Structure describing the command attributes, see shz::DrawIndirectAttribs for details.
		//
		// Draw command arguments are read from the attributes buffer at the offset given by
		// Attribs.IndirectDrawArgsOffset. If `Attribs.DrawCount > 1`, the arguments for command N
		// will be read at the offset
		// 
		//     Attribs.IndirectDrawArgsOffset + N * Attribs.IndirectDrawArgsStride.
		// 
		// If pCountBuffer is not null, the number of commands to execute will be read from the buffer at the offset
		// given by Attribs.CounterOffset. The number of commands will be the lesser of the value read from the buffer
		// and Attribs.DrawCount:
		// 
		//     NumCommands = min(CountBuffer[Attribs.CounterOffset], Attribs.DrawCount)
		// 
		// If `Attribs.IndirectAttribsBufferStateTransitionMode` or `Attribs.CounterBufferStateTransitionMode` is
		// shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, the method may transition the state of the indirect
		// draw arguments buffer, and the state of the counter buffer . This is not a thread safe operation,
		// so no other thread is allowed to read or write the state of the buffer.
		// 
		// If shz::DRAW_FLAG_VERIFY_STATES flag is set, the method reads the state of vertex/index
		// buffers, so no other threads are allowed to alter the states of the same resources.
		// It is OK to read these states.
		// 
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks Supported contexts: graphics.
		virtual void DrawIndirect(const DrawIndirectAttribs& Attribs) = 0;


		// Executes an indexed indirect draw command.

		// \param [in] Attribs - Structure describing the command attributes, see shz::DrawIndexedIndirectAttribs for details.
		//
		// Draw command arguments are read from the attributes buffer at the offset given by
		// Attribs.IndirectDrawArgsOffset. If `Attribs.DrawCount > 1`, the arguments for command N
		// will be read at the offset
		// 
		//     Attribs.IndirectDrawArgsOffset + N * Attribs.IndirectDrawArgsStride.
		// 
		// If pCountBuffer is not null, the number of commands to execute will be read from the buffer at the offset
		// given by Attribs.CounterOffset. The number of commands will be the lesser of the value read from the buffer
		// and Attribs.DrawCount:
		// 
		//     NumCommands = min(CountBuffer[Attribs.CounterOffset], Attribs.DrawCount)
		// 
		// If `Attribs.IndirectAttribsBufferStateTransitionMode` or `Attribs.CounterBufferStateTransitionMode` is
		// shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, the method may transition the state of the indirect
		// draw arguments buffer, and the state of the counter buffer . This is not a thread safe operation,
		// so no other thread is allowed to read or write the state of the buffer.
		// 
		// If shz::DRAW_FLAG_VERIFY_STATES flag is set, the method reads the state of vertex/index
		// buffers, so no other threads are allowed to alter the states of the same resources.
		// It is OK to read these states.
		// 
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks  In OpenGL backend, index buffer offset set by SetIndexBuffer() can't be applied in
		//           indirect draw command and must be zero.
		//
		// \remarks Supported contexts: graphics.
		virtual void DrawIndexedIndirect(const DrawIndexedIndirectAttribs& Attribs) = 0;


		// Executes a mesh draw command.

		// \param [in] Attribs - Draw command attributes, see shz::DrawMeshAttribs for details.
		//
		// For compatibility between Direct3D12 and Vulkan, only a single work group dimension is used.
		// Also in the shader, `numthreads` and `local_size` attributes must define only the first dimension,
		// for example: `[numthreads(ThreadCount, 1, 1)]` or `layout(local_size_x = ThreadCount) in`.
		//
		// \remarks Supported contexts: graphics.
		virtual void DrawMesh(const DrawMeshAttribs& Attribs) = 0;


		// Executes an indirect mesh draw command.

		// \param [in] Attribs - Structure describing the command attributes, see shz::DrawMeshIndirectAttribs for details.
		//
		// For compatibility between Direct3D12 and Vulkan and with direct call (DrawMesh) use the first element in the structure,
		// for example:
		//   * Direct3D12 `{TaskCount, 1, 1}` 
		//   * Vulkan `{TaskCount, 0}`.
		//
		// If `IndirectAttribsBufferStateTransitionMode` member is shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		// the method may transition the state of the indirect draw arguments buffer. This is not a thread safe operation,
		// so no other thread is allowed to read or write the state of the buffer.
		//
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// \remarks Supported contexts: graphics.
		virtual void DrawMeshIndirect(const DrawMeshIndirectAttribs& Attribs) = 0;


		// Executes a multi-draw command.

		// \param [in] Attribs - Multi-draw command attributes, see shz::MultiDrawAttribs for details.
		//
		// If the device does not support the NativeMultiDraw feature, the method will emulate it by
		// issuing a sequence of individual draw commands. Note that draw command index is only
		// available in the shader when the NativeMultiDraw feature is supported.
		//
		// \remarks Supported contexts: graphics.
		virtual void MultiDraw(const MultiDrawAttribs& Attribs) = 0;


		// Executes an indexed multi-draw command.

		// \param [in] Attribs - Multi-draw command attributes, see shz::MultiDrawIndexedAttribs for details.
		//
		// If the device does not support the `NativeMultiDraw` feature, the method will emulate it by
		// issuing a sequence of individual draw commands. Note that draw command index is only
		// available in the shader when the `NativeMultiDraw` feature is supported.
		//
		// \remarks Supported contexts: graphics.
		virtual void MultiDrawIndexed(const MultiDrawIndexedAttribs& Attribs) = 0;


		// Executes a dispatch compute command.

		// \param [in] Attribs - Dispatch command attributes, see shz::DispatchComputeAttribs for details.
		//
		// In Metal, the compute group sizes are defined by the dispatch command rather than by
		// the compute shader. When the shader is compiled from HLSL or GLSL, the engine will
		// use the group size information from the shader. When using MSL, an application should
		// provide the compute group dimensions through `MtlThreadGroupSizeX`, `MtlThreadGroupSizeY`,
		// and `MtlThreadGroupSizeZ` members.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void DispatchCompute(const DispatchComputeAttribs& Attribs) = 0;


		// Executes an indirect dispatch compute command.

		// \param [in] Attribs - The command attributes, see shz::DispatchComputeIndirectAttribs for details.
		//
		// If `IndirectAttribsBufferStateTransitionMode` member is shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		// the method may transition the state of indirect dispatch arguments buffer. This is not a thread safe operation,
		// so no other thread is allowed to read or write the state of the same resource.
		//
		// If the application intends to use the same resources in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// In Metal, the compute group sizes are defined by the dispatch command rather than by
		// the compute shader. When the shader is compiled from HLSL or GLSL, the engine will
		// use the group size information from the shader. When using MSL, an application should
		// provide the compute group dimensions through `MtlThreadGroupSizeX`, `MtlThreadGroupSizeY`,
		// and `MtlThreadGroupSizeZ` members.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void DispatchComputeIndirect(const DispatchComputeIndirectAttribs& Attribs) = 0;


		// Executes a dispatch tile command.

		// \param [in] Attribs - The command attributes, see shz::DispatchTileAttribs for details.
		virtual void DispatchTile(const DispatchTileAttribs& Attribs) = 0;


		// Returns current render pass tile size.

		// \param [out] TileSizeX - Tile size in X direction.
		// \param [out] TileSizeY - Tile size in Y direction.
		//
		// \remarks Result will be zero if there are no active render pass or render targets.
		virtual void GetTileSize(uint32& TileSizeX, uint32& TileSizeY) = 0;


		// Clears a depth-stencil view.

		// \param [in] pView               - Pointer to ITextureView interface to clear. The view type must be
		//                                   shz::TEXTURE_VIEW_DEPTH_STENCIL.
		// \param [in] StateTransitionMode - state transition mode of the depth-stencil buffer to clear.
		// \param [in] ClearFlags          - Indicates which parts of the buffer to clear, see shz::CLEAR_DEPTH_STENCIL_FLAGS.
		// \param [in] fDepth              - Value to clear depth part of the view with.
		// \param [in] Stencil             - Value to clear stencil part of the view with.
		//
		// \remarks The full extent of the view is always cleared. Viewport and scissor settings are not applied.
		//
		// \note The depth-stencil view must be bound to the pipeline for clear operation to be performed.
		//
		// When `StateTransitionMode` is shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, the method will
		// transition the state of the texture to the state required by clear operation.
		// In Direct3D12, this state is always shz::RESOURCE_STATE_DEPTH_WRITE, however in Vulkan
		// the state depends on whether the depth buffer is bound to the pipeline.
		//
		// Resource state transitioning is **not thread-safe**, so no other thread is allowed to read or write
		// the state of resources used by the command.
		//
		// \remarks Supported contexts: graphics.
		virtual void ClearDepthStencil(
			ITextureView* pView,
			CLEAR_DEPTH_STENCIL_FLAGS ClearFlags,
			float fDepth,
			uint8 Stencil,
			RESOURCE_STATE_TRANSITION_MODE StateTransitionMode) = 0;


		// Clears a render target view

		// \param [in] pView               - Pointer to ITextureView interface to clear. The view type must be
		//                                   shz::TEXTURE_VIEW_RENDER_TARGET.
		// \param [in] RGBA                - A 4-component array that represents the color to fill the render target with:
		//                                   - Float values for floating point render target formats.
		//                                   - uint32 values for unsigned integer render target formats.
		//                                   - int32 values for signed integer render target formats.
		//                                   If nullptr is provided, the default array {0,0,0,0} will be used.
		// \param [in] StateTransitionMode - Defines required state transitions (see shz::RESOURCE_STATE_TRANSITION_MODE)
		//
		// The full extent of the view is always cleared. Viewport and scissor settings are not applied.
		//
		// \note
		//     The render target view must be bound to the pipeline for clear operation to be performed in OpenGL backend.
		//
		// When StateTransitionMode is shz::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, the method will
		// transition the texture to the state required by the command. Resource state transitioning is not
		// thread safe, so no other thread is allowed to read or write the states of the same textures.
		//
		// If the application intends to use the same resource in other threads simultaneously, it needs to
		// explicitly manage the states using TransitionResourceStates() method.
		//
		// In D3D12 backend, clearing render targets requires textures to always be transitioned to
		// shz::RESOURCE_STATE_RENDER_TARGET state. In Vulkan backend however this depends on whether a
		// render pass has been started. To clear render target outside of a render pass, the texture must be transitioned to
		// shz::RESOURCE_STATE_COPY_DEST state. Inside a render pass it must be in shz::RESOURCE_STATE_RENDER_TARGET
		// state. When using shz::RESOURCE_STATE_TRANSITION_TRANSITION mode, the engine takes care of proper
		// resource state transition, otherwise it is the responsibility of the application.
		//
		// \remarks Supported contexts: graphics.
		virtual void ClearRenderTarget(
			ITextureView* pView,
			const void* RGBA,
			RESOURCE_STATE_TRANSITION_MODE StateTransitionMode) = 0;


		// Finishes recording commands and generates a command list.

		// \param [out] ppCommandList - Memory location where pointer to the recorded command list will be written.
		virtual void FinishCommandList(ICommandList** ppCommandList) = 0;


		// Submits an array of recorded command lists for execution.

		// \param [in] NumCommandLists - The number of command lists to execute.
		// \param [in] ppCommandLists  - Pointer to the array of NumCommandLists command lists to execute.
		// \remarks After a command list is executed, it is no longer valid and must be released.
		virtual void ExecuteCommandLists(uint32 NumCommandLists, ICommandList* const* ppCommandLists) = 0;


		// Tells the GPU to set a fence to a specified value after all previous work has completed.

		// \param [in] pFence - The fence to signal.
		// \param [in] Value  - The value to set the fence to. This value must be greater than the
		//                      previously signaled value on the same fence.
		//
		// \note The method does not flush the context (an application can do this explicitly if needed)
		//       and the fence will be signaled only when the command context is flushed next time.
		//       If an application needs to wait for the fence in a loop, it must flush the context
		//       after signalling the fence.
		//
		// \note
		// * In Direct3D11 backend, the access to the fence is not thread-safe and
		//   must be externally synchronized.
		// * In Direct3D12 and Vulkan backends, the access to the fence is thread-safe.
		virtual void EnqueueSignal(IFence* pFence, uint64     Value) = 0;


		// Waits until the specified fence reaches or exceeds the specified value, on the device.

		// \param [in] pFence - The fence to wait. Fence must be created with type FENCE_TYPE_GENERAL.
		// \param [in] Value  - The value that the context is waiting for the fence to reach.
		//
		// If NativeFence feature is not enabled (see shz::DeviceFeatures) then
		// Value must be less than or equal to the last signaled or pending value.
		// Value becomes pending when the context is flushed.
		// Waiting for a value that is greater than any pending value will cause a deadlock.
		//
		// \note  If NativeFence feature is enabled then waiting for a value that is greater than
		//        any pending value will cause a GPU stall.
		//
		// In Direct3D12 and Vulkan backend, the access to the fence is thread-safe.
		//
		// \remarks  Wait is only allowed for immediate contexts.
		virtual void DeviceWaitForFence(IFence* pFence, uint64   Value) = 0;

		// Submits all outstanding commands for execution to the GPU and waits until they are complete.

		// \note The method blocks the execution of the calling thread until the wait is complete.
		//
		// Only immediate contexts can be idled.
		// 
		// The methods implicitly flushes the context (see Flush()), so an
		// application must explicitly reset the PSO and bind all required shader resources after
		// idling the context.
		virtual void WaitForIdle() = 0;


		// Marks the beginning of a query.

		// \param [in] pQuery - A pointer to a query object.
		//
		// \remarks    Only immediate contexts can begin a query.
		//
		// Vulkan requires that a query must either begin and end inside the same
		// subpass of a render pass instance, or must both begin and end outside of
		// a render pass instance. This means that an application must either begin
		// and end a query while preserving render targets, or begin it when no render
		// targets are bound to the context. In the latter case the engine will automatically
		// end the render pass, if needed, when the query is ended.
		// Also note that resource transitions must be performed outside of a render pass,
		// and may thus require ending current render pass.
		// To explicitly end current render pass, call
		// SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE).
		//
		// \warning    OpenGL and Vulkan do not support nested queries of the same type.
		//
		// \note       On some devices, queries for a single draw or dispatch command may not be supported.
		//             In this case, the query will begin at the next available moment (for example,
		//             when the next render pass begins or ends).
		//
		// \remarks Supported contexts for graphics queries: graphics.
		//          Supported contexts for time queries:     graphics, compute.
		virtual void BeginQuery(IQuery* pQuery) = 0;


		// Marks the end of a query.

		// \param [in] pQuery - A pointer to a query object.
		//
		// \remarks    A query must be ended by the same context that began it.
		//
		// In Direct3D12 and Vulkan, queries (except for timestamp queries)
		// cannot span command list boundaries, so the engine will never flush
		// the context even if the number of commands exceeds the user-specified limit
		// when there is an active query.
		// It is an error to explicitly flush the context while a query is active.
		//
		// All queries must be ended when FinishFrame() is called.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void EndQuery(IQuery* pQuery) = 0;


		// Submits all pending commands in the context for execution to the command queue.

		// Only immediate contexts can be flushed.
		// 
		// Internally the method resets the state of the current command list/buffer.
		// When the next draw command is issued, the engine will restore all states
		// (rebind render targets and depth-stencil buffer as well as index and vertex buffers,
		// restore viewports and scissor rects, etc.) except for the pipeline state and shader resource
		// bindings. An application must explicitly reset the PSO and bind all required shader
		// resources after flushing the context.
		virtual void Flush() = 0;


		// Updates the data in the buffer.

		// \param [in] pBuffer             - Pointer to the buffer to update.
		// \param [in] Offset              - Offset in bytes from the beginning of the buffer to the update region.
		// \param [in] Size                - Size in bytes of the data region to update.
		// \param [in] pData               - Pointer to the data to write to the buffer.
		// \param [in] StateTransitionMode - Buffer state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE)
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void UpdateBuffer(
			IBuffer* pBuffer,
			uint64 Offset,
			uint64 Size,
			const void* pData,
			RESOURCE_STATE_TRANSITION_MODE StateTransitionMode) = 0;


		// Copies the data from one buffer to another.

		// \param [in] pSrcBuffer              - Source buffer to copy data from.
		// \param [in] SrcOffset               - Offset in bytes from the beginning of the source buffer to the beginning of data to copy.
		// \param [in] SrcBufferTransitionMode - State transition mode of the source buffer (see shz::RESOURCE_STATE_TRANSITION_MODE).
		// \param [in] pDstBuffer              - Destination buffer to copy data to.
		// \param [in] DstOffset               - Offset in bytes from the beginning of the destination buffer to the beginning
		//                                       of the destination region.
		// \param [in] Size                    - Size in bytes of data to copy.
		// \param [in] DstBufferTransitionMode - State transition mode of the destination buffer (see shz::RESOURCE_STATE_TRANSITION_MODE).
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void CopyBuffer(
			IBuffer* pSrcBuffer,
			uint64 SrcOffset,
			RESOURCE_STATE_TRANSITION_MODE SrcBufferTransitionMode,
			IBuffer* pDstBuffer,
			uint64 DstOffset,
			uint64 Size,
			RESOURCE_STATE_TRANSITION_MODE DstBufferTransitionMode) = 0;


		// Maps the buffer.

		// \param [in] pBuffer      - Pointer to the buffer to map.
		// \param [in] MapType      - Type of the map operation. See shz::MAP_TYPE.
		// \param [in] MapFlags     - Special map flags. See shz::MAP_FLAGS.
		// \param [out] pMappedData - Reference to the void pointer to store the address of the mapped region.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void MapBuffer(
			IBuffer* pBuffer,
			MAP_TYPE MapType,
			MAP_FLAGS MapFlags,
			void*& pMappedData) = 0;


		// Unmaps the previously mapped buffer.

		// \param [in] pBuffer - Pointer to the buffer to unmap.
		// \param [in] MapType - Type of the map operation. This parameter must match the type that was
		//                       provided to the Map() method.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void UnmapBuffer(IBuffer* pBuffer, MAP_TYPE   MapType) = 0;


		// Updates the data in the texture.

		// \param [in] pTexture    - Pointer to the device context interface to be used to perform the operation.
		// \param [in] MipLevel    - Mip level of the texture subresource to update.
		// \param [in] Slice       - Array slice. Should be 0 for non-array textures.
		// \param [in] DstBox      - Destination region on the texture to update.
		// \param [in] SubresData  - Source data to copy to the texture.
		// \param [in] SrcBufferTransitionMode - If pSrcBuffer member of TextureSubResData structure is not null, this
		//                                       parameter defines state transition mode of the source buffer.
		//                                       If pSrcBuffer is null, this parameter is ignored.
		// \param [in] TextureTransitionMode   - Texture state transition mode (see shz::RESOURCE_STATE_TRANSITION_MODE)
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void UpdateTexture(
			ITexture* pTexture,
			uint32 MipLevel,
			uint32 Slice,
			const Box& DstBox,
			const TextureSubResData& SubresData,
			RESOURCE_STATE_TRANSITION_MODE SrcBufferTransitionMode,
			RESOURCE_STATE_TRANSITION_MODE TextureTransitionMode) = 0;


		// Copies data from one texture to another.

		// \param [in] CopyAttribs - Structure describing copy command attributes, see shz::CopyTextureAttribs for details.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void CopyTexture(const CopyTextureAttribs& CopyAttribs) = 0;


		// Maps the texture subresource.

		// \param [in] pTexture    - Pointer to the texture to map.
		// \param [in] MipLevel    - Mip level to map.
		// \param [in] ArraySlice  - Array slice to map. This parameter must be 0 for non-array textures.
		// \param [in] MapType     - Type of the map operation. See shz::MAP_TYPE.
		// \param [in] MapFlags    - Special map flags. See shz::MAP_FLAGS.
		// \param [in] pMapRegion  - Texture region to map. If this parameter is null, the entire subresource is mapped.
		// \param [out] MappedData - Mapped texture region data
		//
		// This method is supported in D3D11, D3D12 and Vulkan backends. In D3D11 backend, only the entire
		// subresource can be mapped, so pMapRegion must either be null, or cover the entire subresource.
		// In D3D11 and Vulkan backends, dynamic textures are no different from non-dynamic textures, and mapping
		// with MAP_FLAG_DISCARD has exactly the same behavior.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void MapTextureSubresource(
			ITexture* pTexture,
			uint32 MipLevel,
			uint32 ArraySlice,
			MAP_TYPE MapType,
			MAP_FLAGS MapFlags,
			const Box* pMapRegion,
			MappedTextureSubresource& MappedData) = 0;


		// Unmaps the texture subresource.

		// \param [in] pTexture    - Pointer to the texture to unmap.
		// \param [in] MipLevel    - Mip level to unmap.
		// \param [in] ArraySlice  - Array slice to unmap. This parameter must be 0 for non-array textures.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void UnmapTextureSubresource(
			ITexture* pTexture,
			uint32 MipLevel,
			uint32 ArraySlice) = 0;


		// Generates a mipmap chain.

		// \param [in] pTextureView - Texture view to generate mip maps for.
		// \remarks This function can only be called for a shader resource view.
		//          The texture must be created with shz::MISC_TEXTURE_FLAG_GENERATE_MIPS flag.
		//
		// \remarks Supported contexts: graphics.
		virtual void GenerateMips(ITextureView* pTextureView) = 0;


		// Finishes the current frame and releases dynamic resources allocated by the context.

		// For immediate context, this method is called automatically by ISwapChain::Present() of the primary
		// swap chain, but can also be called explicitly. For deferred contexts, the method must be called by the
		// application to release dynamic resources. The method has some overhead, so it is better to call it once
		// per frame, though it can be called with different frequency. Note that unless the GPU is idled,
		// the resources may actually be released several frames after the one they were used in last time.
		//
		// \note After the call all dynamic resources become invalid and must be written again before the next use.
		//       Also, all committed resources become invalid.
		//
		// For deferred contexts, this method must be called after all command lists referencing dynamic resources
		// have been executed through immediate context.
		//
		// The method does not Flush() the context.
		virtual void FinishFrame() = 0;


		// Returns the current frame number.

		// \note The frame number is incremented every time FinishFrame() is called.
		virtual uint64 GetFrameNumber() const = 0;


		// Transitions resource states.

		// \param [in] BarrierCount      - Number of barriers in pResourceBarriers array
		// \param [in] pResourceBarriers - Pointer to the array of resource barriers
		//
		// When both old and new states are shz::RESOURCE_STATE_UNORDERED_ACCESS, the engine
		// executes UAV barrier on the resource. The barrier makes sure that all UAV accesses
		// (reads or writes) are complete before any future UAV accesses (read or write) can begin.\n
		//
		// There are two main usage scenarios for this method:
		// 1. An application knows specifics of resource state transitions not available to the engine.
		//    For example, only single mip level needs to be transitioned.
		// 2. An application manages resource states in multiple threads in parallel.
		//
		// The method always reads the states of all resources to transition. If the state of a resource is managed
		// by multiple threads in parallel, the resource must first be transitioned to unknown state
		// (shz::RESOURCE_STATE_UNKNOWN) to disable automatic state management in the engine.
		//
		// When StateTransitionDesc::UpdateResourceState is set to true, the method may update the state of the
		// corresponding resource which is **not thread-safe**. No other threads should read or write the state of that
		// resource.
		//
		// \note  Resource states for shader access (e.g. shz::RESOURCE_STATE_CONSTANT_BUFFER,
		//        shz::RESOURCE_STATE_UNORDERED_ACCESS, shz::RESOURCE_STATE_SHADER_RESOURCE)
		//        may map to different native state depending on what context type is used (see DeviceContextDesc::ContextType).
		//        To synchronize write access in compute shader in compute context with a pixel shader read in graphics context, an
		//        application should call TransitionResourceStates() in graphics context.
		//        Using TransitionResourceStates() with NewState = shz::RESOURCE_STATE_SHADER_RESOURCE will not invalidate cache in
		//        graphics shaders and may cause undefined behaviour.
		virtual void TransitionResourceStates(uint32 BarrierCount, const StateTransitionDesc* pResourceBarriers) = 0;


		// Resolves a multi-sampled texture subresource into a non-multi-sampled texture subresource.

		// \param [in] pSrcTexture    - Source multi-sampled texture.
		// \param [in] pDstTexture    - Destination non-multi-sampled texture.
		// \param [in] ResolveAttribs - Resolve command attributes, see shz::ResolveTextureSubresourceAttribs for details.
		//
		// \remarks Supported contexts: graphics.ResolveTextureSubresource
		virtual void ResolveTextureSubresource(
			ITexture* pSrcTexture,
			ITexture* pDstTexture,
			const ResolveTextureSubresourceAttribs& ResolveAttribs) = 0;


		// Builds a bottom-level acceleration structure with the specified geometries.

		// \param [in] Attribs - Structure describing build BLAS command attributes, see shz::BuildBLASAttribs for details.
		//
		// \note Don't call build or copy operation on the same BLAS in a different contexts, because BLAS has CPU-side data
		//       that will not match with GPU-side, so shader binding were incorrect.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void BuildBLAS(const BuildBLASAttribs& Attribs) = 0;


		// Builds a top-level acceleration structure with the specified instances.

		// \param [in] Attribs - Structure describing build TLAS command attributes, see shz::BuildTLASAttribs for details.
		//
		// \note Don't call build or copy operation on the same TLAS in a different contexts, because TLAS has CPU-side data
		//       that will not match with GPU-side, so shader binding were incorrect.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void BuildTLAS(const BuildTLASAttribs& Attribs) = 0;


		// Copies data from one acceleration structure to another.

		// \param [in] Attribs - Structure describing copy BLAS command attributes, see shz::CopyBLASAttribs for details.
		//
		// \note Don't call build or copy operation on the same BLAS in a different contexts, because BLAS has CPU-side data
		//       that will not match with GPU-side, so shader binding were incorrect.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void CopyBLAS(const CopyBLASAttribs& Attribs) = 0;


		// Copies data from one acceleration structure to another.

		// \param [in] Attribs - Structure describing copy TLAS command attributes, see shz::CopyTLASAttribs for details.
		//
		// \note Don't call build or copy operation on the same TLAS in a different contexts, because TLAS has CPU-side data
		//       that will not match with GPU-side, so shader binding were incorrect.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void CopyTLAS(const CopyTLASAttribs& Attribs) = 0;


		// Writes a bottom-level acceleration structure memory size required for compacting operation to a buffer.

		// \param [in] Attribs - Structure describing write BLAS compacted size command attributes, see shz::WriteBLASCompactedSizeAttribs for details.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void WriteBLASCompactedSize(const WriteBLASCompactedSizeAttribs& Attribs) = 0;


		// Writes a top-level acceleration structure memory size required for compacting operation to a buffer.

		// \param [in] Attribs - Structure describing write TLAS compacted size command attributes, see shz::WriteTLASCompactedSizeAttribs for details.
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void WriteTLASCompactedSize(const WriteTLASCompactedSizeAttribs& Attribs) = 0;


		// Executes a trace rays command.

		// \param [in] Attribs - Trace rays command attributes, see shz::TraceRaysAttribs for details.
		//
		// \note   The method is not thread-safe. An application must externally synchronize the access
		//         to the shader binding table (SBT) passed as an argument to the function.
		//         The function does not modify the state of the SBT and can be executed in parallel with other
		//         functions that don't modify the SBT (e.g. TraceRaysIndirect).
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void TraceRays(const TraceRaysAttribs& Attribs) = 0;


		// Executes an indirect trace rays command.

		// \param [in] Attribs - Indirect trace rays command attributes, see shz::TraceRaysIndirectAttribs.
		//
		// \note   The method is not thread-safe. An application must externally synchronize the access
		//         to the shader binding table (SBT) passed as an argument to the function.
		//         The function does not modify the state of the SBT and can be executed in parallel with other
		//         functions that don't modify the SBT (e.g. TraceRays).
		//
		// \remarks Supported contexts: graphics, compute.
		virtual void TraceRaysIndirect(const TraceRaysIndirectAttribs& Attribs) = 0;


		// Updates SBT with the pending data that were recorded in IShaderBindingTable::Bind*** calls.

		// \param [in] pSBT                         - Shader binding table that will be updated if there are pending data.
		// \param [in] pUpdateIndirectBufferAttribs - Indirect ray tracing attributes buffer update attributes (optional, may be null).
		//
		// When `pUpdateIndirectBufferAttribs` is not null, the indirect ray tracing attributes will be written to the `pAttribsBuffer` buffer
		// specified by the structure and can be used by TraceRaysIndirect() command.
		// In Direct3D12 backend, the pAttribsBuffer buffer will be initialized with `D3D12_DISPATCH_RAYS_DESC` structure that contains
		// GPU addresses of the ray tracing shaders in the first 88 bytes and 12 bytes for dimension
		// (see TraceRaysIndirect() description).
		//
		// In Vulkan backend, the `pAttribsBuffer` buffer will not be modified, because the SBT is used directly
		// in TraceRaysIndirect().
		//
		// \remarks  The method is not thread-safe. An application must externally synchronize the access
		//           to the shader binding table (SBT) passed as an argument to the function.
		//           The function modifies the data in the SBT and must not run in parallel with any other command that uses the same SBT.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void UpdateSBT(IShaderBindingTable* pSBT, const UpdateIndirectRTBufferAttribs* pUpdateIndirectBufferAttribs = nullptr) = 0;


		// Stores a pointer to the user-provided data object.

		// The pointer may later be retrieved through GetUserData().
		//
		// \param [in] pUserData - Pointer to the user data object to store.
		//
		// \note   The method is not thread-safe and an application
		//         must externally synchronize the access.
		//
		// The method keeps strong reference to the user data object.
		// If an application needs to release the object, it
		// should call SetUserData(nullptr);
		virtual void SetUserData(IObject* pUserData) = 0;


		// Returns a pointer to the user data object previously

		// set with SetUserData() method.
		//
		// \return     The pointer to the user data object.
		//
		// \remarks    The method does *NOT* call AddRef()
		//             for the object being returned.
		virtual IObject* GetUserData() const = 0;


		// Begins a debug group with name and color.

		// External debug tools may use this information when displaying context commands.
		//
		// \param [in] Name   - Group name.
		// \param [in] pColor - Region color.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		virtual void BeginDebugGroup(const Char* Name,
			const float* pColor = nullptr) = 0;

		// Ends a debug group that was previously started with BeginDebugGroup().
		virtual void EndDebugGroup() = 0;


		// Inserts a debug label with name and color.

		// External debug tools may use this information when displaying context commands.
		//
		// \param [in] Label  - Label name.
		// \param [in] pColor - Label color.
		//
		// \remarks Supported contexts: graphics, compute, transfer.
		//          Not supported in Metal backend.
		virtual void InsertDebugLabel(const Char* Label, const float* pColor = nullptr) = 0;

		// Locks the internal mutex and returns a pointer to the command queue that is associated with this device context.

		// \return A pointer to ICommandQueue interface of the command queue associated with the context.
		//
		// Only immediate device contexts have associated command queues.
		//
		// The engine locks the internal mutex to prevent simultaneous access to the command queue.
		// An application must release the lock by calling UnlockCommandQueue()
		// when it is done working with the queue or the engine will not be able to submit any command
		// list to the queue. Nested calls to LockCommandQueue() are not allowed.
		// The queue pointer never changes while the context is alive, so an application may cache and
		// use the pointer if it does not need to prevent potential simultaneous access to the queue from
		// other threads.
		// 
		// The engine manages the lifetimes of command queues and all other device objects,
		// so an application must not call AddRef/Release methods on the returned interface.
		virtual ICommandQueue* LockCommandQueue() = 0;

		// Unlocks the command queue that was previously locked by LockCommandQueue().
		virtual void UnlockCommandQueue() = 0;


		// Sets the shading base rate and combiners.

		// \param [in] BaseRate          - Base shading rate used for combiner operations.
		// \param [in] PrimitiveCombiner - Combiner operation for the per primitive shading rate (the output of the vertex or geometry shader).
		// \param [in] TextureCombiner   - Combiner operation for texture-based shading rate (fetched from the shading rate texture),
		//                                 see SetRenderTargetsAttribs::pShadingRateMap and SubpassDesc::pShadingRateAttachment.
		//
		// The final shading rate is calculated before the triangle is rasterized by the following algorithm:
		//
		//     PrimitiveRate = ApplyCombiner(PrimitiveCombiner, BaseRate, PerPrimitiveRate)
		//     FinalRate     = ApplyCombiner(TextureCombiner, PrimitiveRate, TextureRate)
		//
		// Where
		//
		//     PerPrimitiveRate - vertex shader output value (HLSL: SV_ShadingRate; GLSL: gl_PrimitiveShadingRateEXT).
		//     TextureRate      - texel value from the shading rate texture, see SetRenderTargetsAttribs::pShadingRateMap.
		// 
		//     SHADING_RATE ApplyCombiner(SHADING_RATE_COMBINER Combiner, SHADING_RATE OriginalRate, SHADING_RATE NewRate)
		//     {
		//         switch (Combiner)
		//         {
		//             case SHADING_RATE_COMBINER_PASSTHROUGH: return OriginalRate;
		//             case SHADING_RATE_COMBINER_OVERRIDE:    return NewRate;
		//             case SHADING_RATE_COMBINER_MIN:         return Min(OriginalRate, NewRate);
		//             case SHADING_RATE_COMBINER_MAX:         return Max(OriginalRate, NewRate);
		//             case SHADING_RATE_COMBINER_SUM:         return OriginalRate + NewRate;
		//             case SHADING_RATE_COMBINER_MUL:         return OriginalRate * NewRate;
		//         }
		//     }
		//
		// * If shz::SHADING_RATE_CAP_FLAG_PER_PRIMITIVE capability is not supported, then PrimitiveCombiner
		//   must be shz::SHADING_RATE_COMBINER_PASSTHROUGH.
		// * If shz::SHADING_RATE_CAP_FLAG_TEXTURE_BASED capability is not supported, then TextureCombiner
		//   must be shz::SHADING_RATE_COMBINER_PASSTHROUGH.
		// * TextureCombiner must be one of the supported combiners in ShadingRateProperties::Combiners.
		//   BaseRate must be SHADING_RATE_1X1 or one of the supported rates in ShadingRateProperties::ShadingRates.
		//
		// \remarks Supported contexts: graphics.
		virtual void SetShadingRate(
			SHADING_RATE BaseRate,
			SHADING_RATE_COMBINER PrimitiveCombiner,
			SHADING_RATE_COMBINER TextureCombiner) = 0;


		// Binds or unbinds memory objects to sparse buffer and sparse textures.

		// \param [in] Attribs - command attributes, see shz::BindSparseResourceMemoryAttribs.
		//
		// * Metal backend: since resource uses a single preallocated memory storage,
		//   memory offset is ignored and the driver acquires any free memory block from
		//   the storage.
		//   If there is no free memory in the storage, the region remains unbound.
		//   Access to the device memory object on the GPU side
		//   must be explicitly synchronized using fences.
		//
		// * Direct3D12, Vulkan and Metal backends require explicitly synchronizing
		//   access to the resource using fences or by WaitForIdle().
		//
		// This command implicitly calls Flush().
		//
		// \remarks This command may only be executed by immediate context whose
		//          internal queue supports COMMAND_QUEUE_TYPE_SPARSE_BINDING.
		virtual void BindSparseResourceMemory(const BindSparseResourceMemoryAttribs& Attribs) = 0;

		// Clears the device context statistics.
		virtual void ClearStats() = 0;

		// Returns the device context statistics, see shz::DeviceContextStats.
		virtual const DeviceContextStats& GetStats() const = 0;
	};


} // namespace shz
