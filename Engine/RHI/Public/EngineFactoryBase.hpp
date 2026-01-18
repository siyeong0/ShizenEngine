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
 // Implementation of the shz::EngineFactoryBase template class

#include "Primitives/Object.h"
#include "Engine/RHI/Interface/IEngineFactory.h"
#include "Engine/Core/Memory/Public/DataBlobImpl.hpp"
#include "DefaultShaderSourceStreamFactory.h"
#include "Engine/RHI/Interface/IDearchiver.h"
#include "Engine/Core/Common/Public/DummyReferenceCounters.hpp"
#include "Engine/Core/Memory/Public/EngineMemory.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

namespace shz
{

	const APIInfo& GetAPIInfo();

	// Validates engine create info EngineCI and throws an exception in case of an error.
	void VerifyEngineCreateInfo(const EngineCreateInfo& EngineCI, const GraphicsAdapterInfo& AdapterInfo) noexcept(false);

	// Template class implementing base functionality of the engine factory

	// \tparam BaseInterface - Base interface that this class will inherit
	//                         (shz::IEngineFactoryD3D11, shz::IEngineFactoryD3D12,
	//                          shz::IEngineFactoryVk or shz::IEngineFactoryOpenGL).
	template <class BaseInterface>
	class EngineFactoryBase : public BaseInterface
	{
	public:
		EngineFactoryBase(const INTERFACE_ID& FactoryIID) noexcept
			: m_FactoryIID{ FactoryIID }
			, m_RefCounters{ *this }

		{
		}

		virtual ~EngineFactoryBase() = default;

		virtual void SHZ_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
		{
			if (ppInterface == nullptr)
				return;

			*ppInterface = nullptr;
			if (IID == IID_Unknown || IID == m_FactoryIID || IID == IID_EngineFactory)
			{
				*ppInterface = this;
				(*ppInterface)->AddRef();
			}
		}

		using IObject::QueryInterface;

		virtual ReferenceCounterValueType SHZ_CALL_TYPE AddRef() override final
		{
			return m_RefCounters.AddStrongRef();
		}

		virtual ReferenceCounterValueType SHZ_CALL_TYPE Release() override final
		{
			return m_RefCounters.ReleaseStrongRef();
		}

		virtual IReferenceCounters* SHZ_CALL_TYPE GetReferenceCounters() const override final
		{
			return const_cast<IReferenceCounters*>(static_cast<const IReferenceCounters*>(&m_RefCounters));
		}

		virtual const APIInfo& SHZ_CALL_TYPE GetAPIInfo() const override final
		{
			return shz::GetAPIInfo();
		}

		virtual void SHZ_CALL_TYPE CreateDataBlob(size_t InitialSize, const void* pData, IDataBlob** ppDataBlob) const override final
		{
			if (RefCntAutoPtr<DataBlobImpl> pDataBlob = DataBlobImpl::Create(InitialSize, pData))
				pDataBlob->QueryInterface(IID_DataBlob, ppDataBlob);
		}

		virtual void SHZ_CALL_TYPE CreateDefaultShaderSourceStreamFactory(const Char* SearchDirectories,
			IShaderSourceInputStreamFactory** ppShaderSourceFactory) const override final
		{
			shz::CreateDefaultShaderSourceStreamFactory(SearchDirectories, ppShaderSourceFactory);
		}

		virtual void SHZ_CALL_TYPE SetMessageCallback(DebugMessageCallbackType MessageCallback) const override final
		{
			SetDebugMessageCallback(MessageCallback);
		}

		virtual void SHZ_CALL_TYPE SetMemoryAllocator(IMemoryAllocator* pAllocator) const override final
		{
			SetRawAllocator(pAllocator);
		}

	protected:
		template <typename DearchiverImplType>
		void CreateDearchiver(const DearchiverCreateInfo& CreateInfo,
			IDearchiver** ppDearchiver) const
		{
			if (ppDearchiver == nullptr)
				return;

			*ppDearchiver = nullptr;
			RefCntAutoPtr<DearchiverImplType> pDearchiver{ NEW_RC_OBJ(GetRawAllocator(), "Dearchiver instance", DearchiverImplType)(CreateInfo) };

			if (pDearchiver)
				pDearchiver->QueryInterface(IID_Dearchiver, reinterpret_cast<IObject**>(ppDearchiver));
		}

	private:
		const INTERFACE_ID                        m_FactoryIID;
		DummyReferenceCounters<EngineFactoryBase> m_RefCounters;
	};

} // namespace shz
