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
 // Definition of the shz::IBottomLevelAS interface and related data structures

#include "Primitives/Object.h"
#include "Primitives/FlagEnum.h"
#include "GraphicsTypes.h"
#include "Constants.h"
#include "IBuffer.h"

namespace shz
{
	// {E56F5755-FE5E-496C-BFA7-BCD535360FF7}
	static constexpr INTERFACE_ID IID_BottomLevelAS =
	{ 0xe56f5755, 0xfe5e, 0x496c, {0xbf, 0xa7, 0xbc, 0xd5, 0x35, 0x36, 0xf, 0xf7} };

	static constexpr uint32 INVALID_INDEX = 0xFFFFFFFFU;

	// Defines bottom level acceleration structure triangles description.

	// Triangle geometry description.
	struct BLASTriangleDesc
	{
		// Geometry name.

		// The name is used to map triangle data (BLASBuildTriangleData) to this geometry.
		const Char* GeometryName = nullptr;

		// The maximum vertex count in this geometry.

		// Current number of vertices is defined in BLASBuildTriangleData::VertexCount.
		uint32 MaxVertexCount = 0;

		// The type of vertices in this geometry, see shz::VALUE_TYPE.

		// \remarks Only the following values are allowed: VT_FLOAT32, VT_FLOAT16, VT_INT16.
		//          VT_INT16 defines 16-bit signed normalized vertex components.
		VALUE_TYPE VertexValueType = VT_UNDEFINED;

		// The number of components in the vertex.

		// \remarks Only 2 or 3 are allowed values. For 2-component formats, the third component is assumed 0.
		uint8 VertexComponentCount = 0;

		// The maximum primitive count in this geometry.

		// The current number of primitives is defined in BLASBuildTriangleData::PrimitiveCount.
		uint32 MaxPrimitiveCount = 0;

		// Index type of this geometry, see shz::VALUE_TYPE.

		// Must be VT_UINT16, VT_UINT32 or VT_UNDEFINED.
		// If not defined then vertex array is used instead of indexed vertices.
		VALUE_TYPE IndexType = VT_UNDEFINED;

		// Vulkan only, allows to use transformations in BLASBuildTriangleData.
		bool AllowsTransforms = false;

		bool operator == (const BLASTriangleDesc& rhs) const
		{
			return MaxVertexCount == rhs.MaxVertexCount &&
				VertexValueType == rhs.VertexValueType &&
				VertexComponentCount == rhs.VertexComponentCount &&
				MaxPrimitiveCount == rhs.MaxPrimitiveCount &&
				IndexType == rhs.IndexType &&
				AllowsTransforms == rhs.AllowsTransforms &&
				SafeStrEqual(GeometryName, rhs.GeometryName);
		}
		bool operator != (const BLASTriangleDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};


	// Defines bottom level acceleration structure axis-aligned bounding boxes description.

	// AABB geometry description.
	struct BLASBoundingBoxDesc
	{
		// Geometry name.
		// The name is used to map AABB data (BLASBuildBoundingBoxData) to this geometry.
		const Char* GeometryName = nullptr;

		// The maximum AABB count.
		// Current number of AABBs is defined in BLASBuildBoundingBoxData::BoxCount.
		uint32                    MaxBoxCount = 0;

		constexpr BLASBoundingBoxDesc() noexcept {}

		constexpr BLASBoundingBoxDesc(const Char* _GeometryName, uint32 _MaxBoxCount) noexcept
			: GeometryName(_GeometryName)
			, MaxBoxCount(_MaxBoxCount)
		{
		}

		bool operator == (const BLASBoundingBoxDesc& rhs) const
		{
			return MaxBoxCount == rhs.MaxBoxCount && SafeStrEqual(GeometryName, rhs.GeometryName);
		}
		bool operator != (const BLASBoundingBoxDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};


	// Defines acceleration structures build flags.
	enum RAYTRACING_BUILD_AS_FLAGS : uint8
	{
		RAYTRACING_BUILD_AS_NONE = 0,

		// Indicates that the specified acceleration structure can be updated
		// via IDeviceContext::BuildBLAS() or IDeviceContext::BuildTLAS().
		// With this flag, the acceleration structure may allocate more memory and take more time to build.
		RAYTRACING_BUILD_AS_ALLOW_UPDATE = 0x01,

		// Indicates that the specified acceleration structure can act as the source for
		// a copy acceleration structure command IDeviceContext::CopyBLAS() or IDeviceContext::CopyTLAS()
		// with COPY_AS_MODE_COMPACT mode to produce a compacted acceleration structure.
		// With this flag acceleration structure may allocate more memory and take more time on build.
		RAYTRACING_BUILD_AS_ALLOW_COMPACTION = 0x02,

		// Indicates that the given acceleration structure build should prioritize trace performance over build time.
		RAYTRACING_BUILD_AS_PREFER_FAST_TRACE = 0x04,

		// Indicates that the given acceleration structure build should prioritize build time over trace performance.
		RAYTRACING_BUILD_AS_PREFER_FAST_BUILD = 0x08,

		// Indicates that this acceleration structure should minimize the size of the scratch memory and the final
		// result build, potentially at the expense of build time or trace performance.
		RAYTRACING_BUILD_AS_LOW_MEMORY = 0x10,

		RAYTRACING_BUILD_AS_FLAG_LAST = RAYTRACING_BUILD_AS_LOW_MEMORY
	};
	DEFINE_FLAG_ENUM_OPERATORS(RAYTRACING_BUILD_AS_FLAGS);


	// Bottom-level AS description.
	struct BottomLevelASDesc : public DeviceObjectAttribs
	{
		// Array of triangle geometry descriptions.
		const BLASTriangleDesc* pTriangles = nullptr;

		// The number of triangle geometries in pTriangles array.
		uint32 TriangleCount = 0;

		// Array of AABB geometry descriptions.
		const BLASBoundingBoxDesc* pBoxes = nullptr;

		// The number of AABB geometries in pBoxes array.
		uint32 BoxCount = 0;

		// Ray tracing build flags, see shz::RAYTRACING_BUILD_AS_FLAGS.
		RAYTRACING_BUILD_AS_FLAGS  Flags = RAYTRACING_BUILD_AS_NONE;

		// Size from the result of IDeviceContext::WriteBLASCompactedSize() if this acceleration structure
		// is going to be the target of a compacting copy (IDeviceContext::CopyBLAS() with COPY_AS_MODE_COMPACT).
		uint64 CompactedSize = 0;

		// Defines which immediate contexts are allowed to execute commands that use this BLAS.

		// When ImmediateContextMask contains a bit at position n, the acceleration structure may be
		// used in the immediate context with index n directly (see DeviceContextDesc::ContextId).
		// It may also be used in a command list recorded by a deferred context that will be executed
		// through that immediate context.
		//
		// \remarks    Only specify these bits that will indicate those immediate contexts where the BLAS
		//             will actually be used. Do not set unnecessary bits as this will result in extra overhead.
		uint64 ImmediateContextMask = 1;

		// Tests if two BLAS descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     `true` if all members of the two structures *except for the Name* are equal,
		//             and `false` otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the BLAS properties.
		bool operator == (const BottomLevelASDesc& rhs) const
		{
			if (TriangleCount != rhs.TriangleCount ||
				BoxCount != rhs.BoxCount ||
				Flags != rhs.Flags ||
				CompactedSize != rhs.CompactedSize ||
				ImmediateContextMask != rhs.ImmediateContextMask)
				return false;

			for (uint32 i = 0; i < TriangleCount; ++i)
				if (pTriangles[i] != rhs.pTriangles[i])
					return false;

			for (uint32 i = 0; i < BoxCount; ++i)
				if (pBoxes[i] != rhs.pBoxes[i])
					return false;

			return true;
		}
		bool operator != (const BottomLevelASDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};


	// Defines the scratch buffer info for acceleration structure.
	struct ScratchBufferSizes
	{
		// Scratch buffer size for acceleration structure building,
		// see IDeviceContext::BuildBLAS(), IDeviceContext::BuildTLAS().
		// May be zero if the acceleration structure was created with non-zero CompactedSize.
		uint64 Build = 0;

		// Scratch buffer size for acceleration structure updating,
		// see IDeviceContext::BuildBLAS(), IDeviceContext::BuildTLAS().
		// May be zero if acceleration structure was created without RAYTRACING_BUILD_AS_ALLOW_UPDATE flag.
		// May be zero if acceleration structure was created with non-zero CompactedSize.
		uint64 Update = 0;

		constexpr ScratchBufferSizes() noexcept {}

		constexpr ScratchBufferSizes(uint64 _Build, uint64 _Update) noexcept\
			: Build(_Build)
			, Update(_Update)
		{}
	};


	// Bottom-level AS interface

	// Defines the methods to manipulate a BLAS object
	struct SHZ_INTERFACE IBottomLevelAS : public IDeviceObject
	{
		// Returns the bottom level AS description used to create the object
		virtual const BottomLevelASDesc& SHZ_CALL_TYPE GetDesc() const override = 0;

		// Returns the geometry description index in BottomLevelASDesc::pTriangles or BottomLevelASDesc::pBoxes.

		// \param [in] Name - Geometry name that is specified in BLASTriangleDesc or BLASBoundingBoxDesc.
		// \return Geometry index or INVALID_INDEX if geometry does not exist.
		//
		// \note Access to the BLAS must be externally synchronized.
		virtual uint32 GetGeometryDescIndex(const Char* Name) const = 0;


		// Returns the geometry index that can be used in a shader binding table.

		// \param [in] Name - Geometry name that is specified in BLASTriangleDesc or BLASBoundingBoxDesc.
		// \return Geometry index or INVALID_INDEX if geometry does not exist.
		//
		// \note Access to the BLAS must be externally synchronized.
		virtual uint32 GetGeometryIndex(const Char* Name) const = 0;


		// Returns the geometry count that was used to build AS.
		// Same as BuildBLASAttribs::TriangleDataCount or BuildBLASAttribs::BoxDataCount.

		// \return The number of geometries that was used to build AS.
		//
		// \note Access to the BLAS must be externally synchronized.
		virtual uint32 GetActualGeometryCount() const = 0;


		// Returns the scratch buffer info for the current acceleration structure.

		// \return ScratchBufferSizes object, see shz::ScratchBufferSizes.
		virtual ScratchBufferSizes GetScratchBufferSizes() const = 0;


		// Returns the native acceleration structure handle specific to the underlying graphics API

		// \return pointer to ID3D12Resource interface, for D3D12 implementation\n
		//         VkAccelerationStructure handle, for Vulkan implementation
		virtual uint64 GetNativeHandle() = 0;


		// Sets the acceleration structure usage state.

		// \note This method does not perform state transition, but
		//       resets the internal acceleration structure state to the given value.
		//       This method should be used after the application finished
		//       manually managing the acceleration structure state and wants to hand over
		//       state management back to the engine.
		virtual void SetState(RESOURCE_STATE State) = 0;


		// Returns the internal acceleration structure state
		virtual RESOURCE_STATE GetState() const = 0;
	};


} // namespace shz
