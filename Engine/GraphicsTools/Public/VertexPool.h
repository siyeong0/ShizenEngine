/*
 *  Copyright 2019-2024 Diligent Graphics LLC
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
 // Declaration of the IVertexPool interface and related data structures.

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/Core/Common/Public/StringTools.h"

namespace shz
{

	struct IVertexPool;

	// {7649D93A-E8A8-4BE8-8FEB-24CA8E232179}
	static constexpr INTERFACE_ID IID_VertexPoolAllocation =
	{ 0x7649d93a, 0xe8a8, 0x4be8, {0x8f, 0xeb, 0x24, 0xca, 0x8e, 0x23, 0x21, 0x79} };

	// {972DA1D1-A587-45FE-95FF-831637F37601}
	static constexpr INTERFACE_ID IID_VertexPool =
	{ 0x972da1d1, 0xa587, 0x45fe, {0x95, 0xff, 0x83, 0x16, 0x37, 0xf3, 0x76, 0x1} };


	// Vertex pool allocation.
	struct IVertexPoolAllocation : public IObject
	{
		// Returns the start vertex of the allocation.
		virtual uint32 GetStartVertex() const = 0;

		// Returns the number of vertices in the allocation.
		virtual uint32 GetVertexCount() const = 0;

		// Returns a pointer to the parent vertex pool.
		virtual IVertexPool* GetPool() = 0;

		// Updates internal buffer at the given index.

		// \remarks    This method is a shortcut for GetPool()->Update(Index, pDevice, pContext).
		virtual IBuffer* Update(uint32 Index, IRenderDevice* pDevice, IDeviceContext* pContext) = 0;

		// Returns a pointer to the internal buffer at the given index.

		// \remarks    This method is a shortcut for GetPool()->GetBuffer(Index).
		virtual IBuffer* GetBuffer(uint32 Index) const = 0;

		// Stores a pointer to the user-provided data object, which
		// may later be retrieved through GetUserData().
		//
		// \param [in] pUserData - A pointer to the user data object to store.
		//
		// \note   The method is not thread-safe and the application
		//         must externally synchronize the access.
		virtual void SetUserData(IObject* pUserData) = 0;

		// Returns a pointer to the user data object previously
		// set with the SetUserData() method.
		//
		// \return     A pointer to the user data object.
		virtual IObject* GetUserData() const = 0;
	};


	// Vertex pool usage stats.
	struct VertexPoolUsageStats
	{
		// The total number of vertices in the pool.
		uint64 TotalVertexCount = 0;

		// The number of vertices allocated from the pool.
		uint64 AllocatedVertexCount = 0;

		// Committed memory size, in bytes.
		uint64 CommittedMemorySize = 0;

		// The total memory size used by all allocations, in bytes.
		uint64 UsedMemorySize = 0;

		// The number of allocations.
		uint32 AllocationCount = 0;

		VertexPoolUsageStats& operator+=(const VertexPoolUsageStats& rhs)
		{
			TotalVertexCount += rhs.TotalVertexCount;
			AllocatedVertexCount += rhs.AllocatedVertexCount;
			CommittedMemorySize += rhs.CommittedMemorySize;
			UsedMemorySize += rhs.UsedMemorySize;
			AllocationCount += rhs.AllocationCount;
			return *this;
		}
	};


	// Vertex pool element description.
	struct VertexPoolElementDesc
	{
		// Element size, in bytes.
		uint32 Size = 0;

		// Buffer bind flags, see shz::BIND_FLAGS.
		BIND_FLAGS BindFlags = BIND_VERTEX_BUFFER;

		// Buffer usage, see shz::USAGE.
		USAGE Usage = USAGE_DEFAULT;

		// Buffer mode, see shz::BUFFER_MODE.
		BUFFER_MODE Mode = BUFFER_MODE_UNDEFINED;

		// CPU access flags, see shz::CPU_ACCESS_FLAGS.
		CPU_ACCESS_FLAGS CPUAccessFlags = CPU_ACCESS_NONE;

		constexpr VertexPoolElementDesc() noexcept {}

		constexpr explicit VertexPoolElementDesc(
			uint32           _Size,
			BIND_FLAGS       _BindFlags = VertexPoolElementDesc{}.BindFlags,
			USAGE            _Usage = VertexPoolElementDesc{}.Usage,
			BUFFER_MODE      _Mode = VertexPoolElementDesc{}.Mode,
			CPU_ACCESS_FLAGS _CPUAccessFlags = VertexPoolElementDesc{}.CPUAccessFlags) noexcept
			: Size{ _Size }
			, BindFlags{ _BindFlags }
			, Usage{ _Usage }
			, Mode{ _Mode }
			, CPUAccessFlags{ _CPUAccessFlags }

		{
		}

		constexpr bool operator==(const VertexPoolElementDesc& rhs) const
		{

			return Size == rhs.Size &&
				BindFlags == rhs.BindFlags &&
				Usage == rhs.Usage &&
				Mode == rhs.Mode &&
				CPUAccessFlags == rhs.CPUAccessFlags;

		}

		constexpr bool operator!=(const VertexPoolElementDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};

	// Vertex pool description.
	struct VertexPoolDesc
	{
		// Pool name.
		const char* Name = nullptr;

		// A pointer to the array of pool elements.
		const VertexPoolElementDesc* pElements = nullptr;

		// The number of elements.
		uint32 NumElements = 0;

		// The number of vertices in the pool.
		uint32 VertexCount = 0;

		bool operator==(const VertexPoolDesc& rhs) const
		{
			if (!SafeStrEqual(Name, rhs.Name) ||
				NumElements != rhs.NumElements ||
				VertexCount != rhs.VertexCount)
				return false;

			for (uint32 i = 0; i < NumElements; ++i)
			{
				if (pElements[i] != rhs.pElements[i])
					return false;
			}

			return true;
		}

		bool operator!=(const VertexPoolDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};

	// Vertex pool interface.

	// The vertex pool is a collection of dynamic buffers that can be used to store vertex data.
	struct IVertexPool : public IObject
	{
		// Updates the internal buffer object at the given index.

		// \param[in]  Index    - The vertex buffer index. Must be in range [0, Desc.NumElements-1].
		// \param[in]  pDevice  - A pointer to the render device that will be used to
		//                        create a new internal buffer, if necessary.
		// \param[in]  pContext - A pointer to the device context that will be used to
		//                        copy existing contents to the new buffer, if necessary.
		//
		// \return                A pointer to the internal buffer object.
		//
		// If the internal buffer needs to be resized, `pDevice` and `pContext` will
		// be used to create a new buffer and copy existing contents to the new buffer.
		// The method is not thread-safe and an application must externally synchronize the
		// access.
		virtual IBuffer* Update(uint32 Index, IRenderDevice* pDevice, IDeviceContext* pContext) = 0;

		// Updates all internal buffers.
		//
		// \remarks    This method is equivalent to calling Update() for each internal buffer.
		virtual void UpdateAll(IRenderDevice* pDevice, IDeviceContext* pContext) = 0;

		// Returns a pointer to the internal buffer at the given index.
		//
		// If the internal buffer has not been initialized yet, the method will return null.
		// If the buffer may need to be updated (resized or initialized), use the Update()
		// method.
		virtual IBuffer* GetBuffer(uint32 Index) const = 0;


		// Allocates vertices from the pool.

		// \param[in]  NumVertices  - The number of vertices to allocate.
		// \param[out] ppAllocation - Memory location where pointer to the new allocation will be
		//                            stored.
		//
		// \remarks    The method is thread-safe and can be called from multiple threads simultaneously.
		virtual void Allocate(uint32 NumVertices, IVertexPoolAllocation** ppAllocation) = 0;


		// Returns the usage stats, see shz::VertexPoolUsageStats.
		virtual void GetUsageStats(VertexPoolUsageStats& UsageStats) = 0;

		// Returns the internal buffer version. The version is incremented every time
		// any internal buffer is recreated.
		virtual uint32 GetVersion() const = 0;

		// Returns the pool description.
		virtual const VertexPoolDesc& GetDesc() const = 0;
	};


	// Vertex pool create information.
	struct VertexPoolCreateInfo
	{
		// Vertex pool description.
		VertexPoolDesc Desc;

		// Pool expansion size, in vertices.

		// When non-zero, the pool will be expanded by the specified number of vertices
		// every time there is insufficient space. If zero, the pool size will be doubled
		// when more space is needed.
		uint32 ExtraVertexCount = 0;

		// The maximum number of vertices that can be stored in the pool.

		// If zero, the number of vertices is unlimited.
		uint32 MaxVertexCount = 0;

		// Whether to disable debug validation of the internal pool structure.

		// By default, internal pool structure is validated in debug
		// mode after each allocation and deallocation. This may be expensive
		// when the pool contains many allocations. When this flag is set
		// to true, the validation is disabled.
		// The flag is ignored in release builds as the validation is always disabled.
		bool DisableDebugValidation = false;


		bool operator==(const VertexPoolCreateInfo& rhs) const
		{
			return Desc == rhs.Desc &&
				ExtraVertexCount == rhs.ExtraVertexCount &&
				MaxVertexCount == rhs.MaxVertexCount &&
				DisableDebugValidation == rhs.DisableDebugValidation;
		}

		bool operator!=(const VertexPoolCreateInfo& rhs) const
		{
			return !(*this == rhs);
		}
	};

	// Creates a new vertex pool.

	// \param[in]  pDevice      - A pointer to the render device that will be used to initialize
	//                            internal buffer objects. If this parameter is null, the
	//                            buffers will be created when Update() is called.
	// \param[in]  CreateInfo   - Vertex pool create info, see shz::VertexPoolCreateInfo.
	// \param[in]  ppVertexPool - Memory location where a pointer to the vertex pool will be stored.
	void CreateVertexPool(IRenderDevice* pDevice, const VertexPoolCreateInfo& CreateInfo, IVertexPool** ppVertexPool);

} // namespace shz
