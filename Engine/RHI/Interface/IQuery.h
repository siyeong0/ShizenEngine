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
 // Defines shz::IQuery interface and related data structures

#include "IDeviceObject.h"
#include "GraphicsTypes.h"

namespace shz
{
	// {70F2A88A-F8BE-4901-8F05-2F72FA695BA0}
	static constexpr INTERFACE_ID IID_Query =
	{ 0x70f2a88a, 0xf8be, 0x4901, {0x8f, 0x5, 0x2f, 0x72, 0xfa, 0x69, 0x5b, 0xa0} };

	// Occlusion query data.

	// This structure is filled by IQuery::GetData() for shz::QUERY_TYPE_OCCLUSION query type.
	struct QueryDataOcclusion
	{
		// Query type - must be shz::QUERY_TYPE_OCCLUSION
		const enum QUERY_TYPE Type = QUERY_TYPE_OCCLUSION;

		// The number of samples that passed the depth and stencil tests in between
		// IDeviceContext::BeginQuery and IDeviceContext::EndQuery.
		uint64 NumSamples = 0;
	};

	// Binary occlusion query data.

	// This structure is filled by IQuery::GetData() for shz::QUERY_TYPE_BINARY_OCCLUSION query type.
	struct QueryDataBinaryOcclusion
	{
		// Query type - must be shz::QUERY_TYPE_BINARY_OCCLUSION
		const enum QUERY_TYPE Type = QUERY_TYPE_BINARY_OCCLUSION;

		// Indicates if at least one sample passed depth and stencil testing in between
		// IDeviceContext::BeginQuery and IDeviceContext::EndQuery.
		bool AnySamplePassed = false;
	};

	// Timestamp query data.

	// This structure is filled by IQuery::GetData() for shz::QUERY_TYPE_TIMESTAMP query type.
	struct QueryDataTimestamp
	{
		// Query type - must be shz::QUERY_TYPE_TIMESTAMP
		const enum QUERY_TYPE Type = QUERY_TYPE_TIMESTAMP;

		// The value of a high-frequency counter.
		uint64 Counter = 0;

		// The counter frequency, in Hz (ticks/second). If there was an error
		// while getting the timestamp, this value will be 0.
		uint64 Frequency = 0;
	};

	// Pipeline statistics query data.

	// This structure is filled by IQuery::GetData() for shz::QUERY_TYPE_PIPELINE_STATISTICS query type.
	//
	// \warning  In OpenGL backend the only field that will be populated is ClippingInvocations.
	struct QueryDataPipelineStatistics
	{
		// Query type - must be shz::QUERY_TYPE_PIPELINE_STATISTICS
		const enum QUERY_TYPE Type = QUERY_TYPE_PIPELINE_STATISTICS;

		// Number of vertices processed by the input assembler stage.
		uint64 InputVertices = 0;

		// Number of primitives processed by the input assembler stage.
		uint64 InputPrimitives = 0;

		// Number of primitives output by a geometry shader.
		uint64 GSPrimitives = 0;

		// Number of primitives that were sent to the clipping stage.
		uint64 ClippingInvocations = 0;

		// Number of primitives that were output by the clipping stage and were rendered.
		// This may be larger or smaller than ClippingInvocations because after a primitive is
		// clipped sometimes it is either broken up into more than one primitive or completely culled.
		uint64 ClippingPrimitives = 0;

		// Number of times a vertex shader was invoked.
		uint64 VSInvocations = 0;

		// Number of times a geometry shader was invoked.
		uint64 GSInvocations = 0;

		// Number of times a pixel shader shader was invoked.
		uint64 PSInvocations = 0;

		// Number of times a hull shader shader was invoked.
		uint64 HSInvocations = 0;

		// Number of times a domain shader shader was invoked.
		uint64 DSInvocations = 0;

		// Number of times a compute shader was invoked.
		uint64 CSInvocations = 0;
	};

	// Duration query data.

	// This structure is filled by IQuery::GetData() for shz::QUERY_TYPE_DURATION query type.
	struct QueryDataDuration
	{
		// Query type - must be shz::QUERY_TYPE_DURATION
		const enum QUERY_TYPE Type = QUERY_TYPE_DURATION;

		// The number of high-frequency counter ticks between
		// BeginQuery and EndQuery calls.
		uint64 Duration = 0;

		// The counter frequency, in Hz (ticks/second). If there was an error
		// while getting the timestamp, this value will be 0.
		uint64 Frequency = 0;
	};

	// Query description.
	struct QueryDesc : public DeviceObjectAttribs
	{

		// Query type, see shz::QUERY_TYPE.
		enum QUERY_TYPE Type = QUERY_TYPE_UNDEFINED;

		constexpr QueryDesc() noexcept {};

		explicit constexpr QueryDesc(QUERY_TYPE _Type) noexcept
			: Type(_Type)
		{
		}
	};

	// Query interface.

	// Defines the methods to manipulate a Query object
	struct SHZ_INTERFACE IQuery : public IDeviceObject
	{
		// Returns the Query description used to create the object.
		virtual const QueryDesc& GetDesc() const override = 0;

		// Gets the query data.

		// \param [in] pData          - Pointer to the query data structure. Depending on the type of the query,
		//                              this must be the pointer to shz::QueryDataOcclusion, shz::QueryDataBinaryOcclusion,
		//                              shz::QueryDataTimestamp, or shz::QueryDataPipelineStatistics
		//                              structure.
		//                              An application may provide nullptr to only check the query status.
		// \param [in] DataSize       - Size of the data structure.
		// \param [in] AutoInvalidate - Whether to invalidate the query if the results are available and release associated resources.
		//                              An application should typically always invalidate completed queries unless
		//                              it needs to retrieve the same data through GetData() multiple times.
		//                              A query will not be invalidated if `pData` is `nullptr`.
		//
		// \return     `true` if the query data is available and `false` otherwise.
		//
		// In Direct3D11 backend timestamp queries will only be available after FinishFrame is called
		// for the frame in which they were collected.
		//
		// If `AutoInvalidate` is set to true, and the data have been retrieved, an application
		// must not call GetData() until it begins and ends the query again.
		virtual bool GetData(void* pData, uint32 DataSize, bool AutoInvalidate = true) = 0;

		// Invalidates the query and releases associated resources.
		virtual void Invalidate() = 0;
	};


} // namespace shz
