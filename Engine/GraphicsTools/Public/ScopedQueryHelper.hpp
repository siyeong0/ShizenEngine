/*
 *  Copyright 2019-2022 Diligent Graphics LLC
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

#include <vector>
#include <deque>

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/IQuery.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

namespace shz
{

	// Helper class to manage scoped queries.

	// One scoped query instance must be used once per frame.
	class ScopedQueryHelper
	{
	public:
		ScopedQueryHelper(
			IRenderDevice* pDevice,
			const QueryDesc& queryDesc,
			uint32           NumQueriesToReserve,
			uint32           ExpectedQueryLimit = 5);


		ScopedQueryHelper(const ScopedQueryHelper&) = delete;
		ScopedQueryHelper& operator=(const ScopedQueryHelper&) = delete;
		ScopedQueryHelper(ScopedQueryHelper&&) = default;
		ScopedQueryHelper& operator=(ScopedQueryHelper&&) = delete;



		// Begins a query.

		// \param [in] pCtx - Context to record begin query command
		//
		// \remarks    There must be exactly one matching Begin() for every End() call, otherwise
		//             the behavior is undefined.
		void Begin(IDeviceContext* pCtx);


		// Ends a query and returns the query data, if it is available.

		// \param [in]  pCtx     - Context to record end query command.
		// \param [out] pData    - Pointer to the buffer that will receive query data.
		// \param [in]  DataSize - Buffer data size.
		// \return                 true if the data from the oldest query is available, and false otherwise.
		//
		// \remarks    There must be exactly one matching End() for every Begin() call, otherwise
		//             the behavior is undefined.
		bool End(IDeviceContext* pCtx, void* pData, uint32 DataSize);

	private:
		RefCntAutoPtr<IRenderDevice> m_pDevice;
		const QueryDesc              m_QueryDesc;
		const uint32                 m_ExpectedQueryLimit;

		std::deque<RefCntAutoPtr<IQuery>>  m_PendingQueries;
		std::vector<RefCntAutoPtr<IQuery>> m_AvailableQueries;
	};

} // namespace shz
