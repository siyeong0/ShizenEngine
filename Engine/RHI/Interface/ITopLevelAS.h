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
 // Definition of the shz::ITopLevelAS interface and related data structures

#include "Primitives/Object.h"
#include "Primitives/FlagEnum.h"
#include "GraphicsTypes.h"
#include "Constants.h"
#include "IBuffer.h"
#include "IBottomLevelAS.h"

namespace shz
{

	// {16561861-294B-4804-96FA-1717333F769A}
	static constexpr INTERFACE_ID IID_TopLevelAS =
	{ 0x16561861, 0x294b, 0x4804, {0x96, 0xfa, 0x17, 0x17, 0x33, 0x3f, 0x76, 0x9a} };


	// Top-level AS description.
	struct TopLevelASDesc : public DeviceObjectAttribs 
	{
		// Allocate space for specified number of instances.
		uint32                    MaxInstanceCount = 0;

		// Ray tracing build flags, see shz::RAYTRACING_BUILD_AS_FLAGS.
		RAYTRACING_BUILD_AS_FLAGS Flags = RAYTRACING_BUILD_AS_NONE;

		// The size returned by IDeviceContext::WriteTLASCompactedSize(), if this acceleration structure
		// is going to be the target of a compacting copy command (IDeviceContext::CopyTLAS() with shz::COPY_AS_MODE_COMPACT).
		uint64                    CompactedSize = 0;

		// Defines which immediate contexts are allowed to execute commands that use this TLAS.

		// When ImmediateContextMask contains a bit at position n, the acceleration structure may be
		// used in the immediate context with index n directly (see DeviceContextDesc::ContextId).
		// It may also be used in a command list recorded by a deferred context that will be executed
		// through that immediate context.
		//
		// \remarks    Only specify these bits that will indicate those immediate contexts where the TLAS
		//             will actually be used. Do not set unnecessary bits as this will result in extra overhead.
		uint64                    ImmediateContextMask = 1;

		constexpr TopLevelASDesc() noexcept {}
	};


	// Defines hit group binding mode used by the top-level AS.
	enum HIT_GROUP_BINDING_MODE : uint8
	{
		// Each geometry in every instance may use a unique hit shader group.
		// In this mode, the SBT reserves space for each geometry in every instance in
		// the TLAS and uses most memory.
		// See IShaderBindingTable::BindHitGroupForGeometry().
		HIT_GROUP_BINDING_MODE_PER_GEOMETRY = 0,

		// Each instance may use a unique hit shader group.
		// In this mode, the SBT reserves one slot for each instance irrespective of
		// how many geometries it contains, so it uses less memory.
		// See IShaderBindingTable::BindHitGroupForInstance().
		HIT_GROUP_BINDING_MODE_PER_INSTANCE,

		// All instances in each TLAS will use the same hit group.
		// In this mode, the SBT reserves a single slot for one hit group for each TLAS
		// and uses least memory.
		// See IShaderBindingTable::BindHitGroupForTLAS().
		HIT_GROUP_BINDING_MODE_PER_TLAS,

		// The user must specify TLASBuildInstanceData::ContributionToHitGroupIndex
		// and only use IShaderBindingTable::BindHitGroupByIndex().
		HIT_GROUP_BINDING_MODE_USER_DEFINED,

		HIT_GROUP_BINDING_MODE_LAST = HIT_GROUP_BINDING_MODE_USER_DEFINED,
	};


	// Defines TLAS state that was used in the last build.
	struct TLASBuildInfo
	{
		// The number of instances, same as BuildTLASAttribs::InstanceCount.
		uint32                 InstanceCount = 0;

		// The number of hit shader groups, same as BuildTLASAttribs::HitGroupStride.
		uint32                 HitGroupStride = 0;

		// Hit group binding mode, same as BuildTLASAttribs::BindingMode.
		HIT_GROUP_BINDING_MODE BindingMode = HIT_GROUP_BINDING_MODE_PER_GEOMETRY;

		// First hit group location, same as BuildTLASAttribs::BaseContributionToHitGroupIndex.
		uint32                 FirstContributionToHitGroupIndex = 0;

		// Last hit group location.
		uint32                 LastContributionToHitGroupIndex = 0;
	};


	// Top-level AS instance description.
	struct TLASInstanceDesc
	{
		// Index that corresponds to the one specified in TLASBuildInstanceData::ContributionToHitGroupIndex.
		uint32          ContributionToHitGroupIndex = 0;

		// The autogenerated index of the instance.
		// Same as InstanceIndex() in HLSL and gl_InstanceID in GLSL.
		uint32          InstanceIndex = 0;

		// Bottom-level AS that is specified in TLASBuildInstanceData::pBLAS.
		IBottomLevelAS* pBLAS = nullptr;

		constexpr TLASInstanceDesc() noexcept {}
	};





	// Top-level AS interface

	// Defines the methods to manipulate a TLAS object
	struct SHZ_INTERFACE ITopLevelAS : public IDeviceObject
	{
		// Returns the top level AS description used to create the object
		virtual const TopLevelASDesc& SHZ_CALL_TYPE GetDesc() const override = 0;

		// Returns instance description that can be used in shader binding table.

		// \param [in] Name - Instance name that is specified in TLASBuildInstanceData::InstanceName.
		// \return TLASInstanceDesc object, see shz::TLASInstanceDesc.
		//         If instance does not exist then TLASInstanceDesc::ContributionToHitGroupIndex
		//         and TLASInstanceDesc::InstanceIndex are set to INVALID_INDEX.
		//
		// \note Access to the TLAS must be externally synchronized.
		virtual TLASInstanceDesc GetInstanceDesc(const Char* Name) const = 0;


		// Returns TLAS state after the last build or update operation.

		// \return TLASBuildInfo object, see shz::TLASBuildInfo.
		//
		// \note Access to the TLAS must be externally synchronized.
		virtual TLASBuildInfo GetBuildInfo() const = 0;


		// Returns scratch buffer info for the current acceleration structure.

		// \return ScratchBufferSizes object, see shz::ScratchBufferSizes.
		virtual ScratchBufferSizes GetScratchBufferSizes() const = 0;


		// Returns native acceleration structure handle specific to the underlying graphics API

		// \return A pointer to `ID3D12Resource` interface, for D3D12 implementation\n
		//         `VkAccelerationStructure` handle, for Vulkan implementation
		virtual uint64 GetNativeHandle() = 0;


		// Sets the acceleration structure usage state.

		// This method does not perform state transition, but
		// resets the internal acceleration structure state to the given value.
		// This method should be used after the application finished
		// manually managing the acceleration structure state and wants to hand over
		// state management back to the engine.
		virtual void SetState(RESOURCE_STATE State) = 0;


		// Returns the internal acceleration structure state
		virtual RESOURCE_STATE GetState() const = 0;
	};


} // namespace shz
