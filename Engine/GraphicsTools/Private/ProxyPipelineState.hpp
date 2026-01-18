/*
 *  Copyright 2024-2025 Diligent Graphics LLC
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
 // Definition of the shz::ProxyPipelineState class

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/RHI/Public/PipelineStateBase.hpp"

namespace shz
{

	// Proxy pipeline state delegates all calls to the internal pipeline object.

	template <typename Base>
	class ProxyPipelineState : public Base
	{
	public:
		template <typename... Args>
		ProxyPipelineState(const PipelineStateDesc& PSODesc, Args&&... args)
			: Base{ std::forward<Args>(args)... }
			, m_Name{ PSODesc.Name != nullptr ? PSODesc.Name : "" }
			, m_Desc{ m_Name.c_str(), PSODesc.PipelineType }
		{
		}

		virtual const PipelineStateDesc& SHZ_CALL_TYPE GetDesc() const override
		{
			return m_pPipeline ? m_pPipeline->GetDesc() : m_Desc;
		}

		virtual int32 SHZ_CALL_TYPE GetUniqueID() const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetUniqueID() : -1;
		}

		virtual void SHZ_CALL_TYPE SetUserData(IObject* pUserData) override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			if (m_pPipeline)
			{
				m_pPipeline->SetUserData(pUserData);
			}
		}

		virtual IObject* SHZ_CALL_TYPE GetUserData() const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetUserData() : nullptr;
		}

		virtual const GraphicsPipelineDesc& SHZ_CALL_TYPE GetGraphicsPipelineDesc() const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			static constexpr GraphicsPipelineDesc NullDesc;
			return m_pPipeline ? m_pPipeline->GetGraphicsPipelineDesc() : NullDesc;
		}

		virtual const RayTracingPipelineDesc& SHZ_CALL_TYPE GetRayTracingPipelineDesc() const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			static constexpr RayTracingPipelineDesc NullDesc;
			return m_pPipeline ? m_pPipeline->GetRayTracingPipelineDesc() : NullDesc;
		}

		virtual const TilePipelineDesc& SHZ_CALL_TYPE GetTilePipelineDesc() const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			static constexpr TilePipelineDesc NullDesc;
			return m_pPipeline ? m_pPipeline->GetTilePipelineDesc() : NullDesc;
		}

		virtual void SHZ_CALL_TYPE BindStaticResources(SHADER_TYPE ShaderStages, IResourceMapping* pResourceMapping, BIND_SHADER_RESOURCES_FLAGS Flags) override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			if (m_pPipeline)
			{
				m_pPipeline->BindStaticResources(ShaderStages, pResourceMapping, Flags);
			}
		}

		virtual uint32 SHZ_CALL_TYPE GetStaticVariableCount(SHADER_TYPE ShaderType) const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetStaticVariableCount(ShaderType) : 0;
		}

		virtual IShaderResourceVariable* SHZ_CALL_TYPE GetStaticVariableByName(SHADER_TYPE ShaderType, const Char* Name) override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetStaticVariableByName(ShaderType, Name) : nullptr;
		}

		virtual IShaderResourceVariable* SHZ_CALL_TYPE GetStaticVariableByIndex(SHADER_TYPE ShaderType, uint32 Index) override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetStaticVariableByIndex(ShaderType, Index) : nullptr;
		}

		virtual void SHZ_CALL_TYPE CreateShaderResourceBinding(IShaderResourceBinding** ppShaderResourceBinding, bool InitStaticResources) override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			if (m_pPipeline)
			{
				m_pPipeline->CreateShaderResourceBinding(ppShaderResourceBinding, InitStaticResources);
			}
		}

		virtual void SHZ_CALL_TYPE InitializeStaticSRBResources(IShaderResourceBinding* pShaderResourceBinding) const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			if (m_pPipeline)
			{
				m_pPipeline->InitializeStaticSRBResources(pShaderResourceBinding);
			}
		}

		virtual void SHZ_CALL_TYPE CopyStaticResources(IPipelineState* pPSO) const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			if (m_pPipeline)
			{
				m_pPipeline->CopyStaticResources(pPSO);
			}
		}

		virtual bool SHZ_CALL_TYPE IsCompatibleWith(const IPipelineState* pPSO) const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->IsCompatibleWith(pPSO) : false;
		}

		virtual uint32 SHZ_CALL_TYPE GetResourceSignatureCount() const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetResourceSignatureCount() : 0;
		}

		virtual IPipelineResourceSignature* SHZ_CALL_TYPE GetResourceSignature(uint32 Index) const override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetResourceSignature(Index) : nullptr;
		}

		virtual PIPELINE_STATE_STATUS SHZ_CALL_TYPE GetStatus(bool WaitForCompletion) override
		{
			ASSERT(m_pPipeline, "Internal pipeline is null");
			return m_pPipeline ? m_pPipeline->GetStatus(WaitForCompletion) : PIPELINE_STATE_STATUS_UNINITIALIZED;
		}

	protected:
		const std::string       m_Name;
		const PipelineStateDesc m_Desc;

		RefCntAutoPtr<IPipelineState> m_pPipeline;
	};

} // namespace shz
