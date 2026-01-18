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

#include "pch.h"

#include "ShaderVariableManagerD3D12.hpp"

#include "RenderDeviceD3D12Impl.hpp"
#include "PipelineResourceSignatureD3D12Impl.hpp"
#include "BufferD3D12Impl.hpp"
#include "BufferViewD3D12Impl.hpp"
#include "SamplerD3D12Impl.hpp"
#include "TextureD3D12Impl.hpp"
#include "TextureViewD3D12Impl.hpp"
#include "TopLevelASD3D12Impl.hpp"

#include "Engine/RHI_D3DBase/Public/ShaderVariableD3D.hpp"
#include "ShaderResourceCacheD3D12.hpp"

namespace shz
{

	template <typename HandlerType>
	void ProcessSignatureResources(
		const PipelineResourceSignatureD3D12Impl& Signature,
		const SHADER_RESOURCE_VARIABLE_TYPE* AllowedVarTypes,
		uint32                                    NumAllowedTypes,
		SHADER_TYPE                               ShaderStages,
		HandlerType&& Handler)
	{
		const bool UsingCombinedSamplers = Signature.IsUsingCombinedSamplers();
		Signature.ProcessResources(AllowedVarTypes, NumAllowedTypes, ShaderStages,
			[&](const PipelineResourceDesc& ResDesc, uint32 Index) //
			{
				const PipelineResourceSignatureD3D12Impl::ResourceAttribs& ResAttr = Signature.GetResourceAttribs(Index);

				// Skip samplers combined with textures and immutable samplers
				if (ResDesc.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER &&
					(UsingCombinedSamplers || ResAttr.IsImmutableSamplerAssigned()))
					return;

				Handler(Index);
			});
	}

	size_t ShaderVariableManagerD3D12::GetRequiredMemorySize(
		const PipelineResourceSignatureD3D12Impl& Signature,
		const SHADER_RESOURCE_VARIABLE_TYPE* AllowedVarTypes,
		uint32                                    NumAllowedTypes,
		SHADER_TYPE                               ShaderStages,
		uint32* pNumVariables)
	{
		uint32 NumVariables = 0;
		if (pNumVariables == nullptr)
			pNumVariables = &NumVariables;
		*pNumVariables = 0;
		ProcessSignatureResources(Signature, AllowedVarTypes, NumAllowedTypes, ShaderStages,
			[pNumVariables](uint32) //
			{
				++(*pNumVariables);
			});

		return (*pNumVariables) * sizeof(ShaderVariableD3D12Impl);
	}

	// Creates shader variable for every resource from Signature whose type is one of AllowedVarTypes
	void ShaderVariableManagerD3D12::Initialize(
		const PipelineResourceSignatureD3D12Impl& Signature,
		IMemoryAllocator& Allocator,
		const SHADER_RESOURCE_VARIABLE_TYPE* AllowedVarTypes,
		uint32                                    NumAllowedTypes,
		SHADER_TYPE                               ShaderType)
	{
		ASSERT_EXPR(m_NumVariables == 0);
		const size_t MemSize = GetRequiredMemorySize(Signature, AllowedVarTypes, NumAllowedTypes, ShaderType, &m_NumVariables);

		if (m_NumVariables == 0)
			return;

		TBase::Initialize(Signature, Allocator, MemSize);

		uint32 VarInd = 0;
		ProcessSignatureResources(Signature, AllowedVarTypes, NumAllowedTypes, ShaderType,
			[this, &VarInd](uint32 ResIndex) //
			{
				::new (m_pVariables + VarInd) ShaderVariableD3D12Impl{ *this, ResIndex };
				++VarInd;
			});
		ASSERT_EXPR(VarInd == m_NumVariables);
	}

	void ShaderVariableManagerD3D12::Destroy(IMemoryAllocator& Allocator)
	{
		if (m_pVariables != nullptr)
		{
			for (uint32 v = 0; v < m_NumVariables; ++v)
				m_pVariables[v].~ShaderVariableD3D12Impl();
		}
		TBase::Destroy(Allocator);
	}

	const PipelineResourceDesc& ShaderVariableManagerD3D12::GetResourceDesc(uint32 Index) const
	{
		ASSERT_EXPR(m_pSignature != nullptr);
		return m_pSignature->GetResourceDesc(Index);
	}

	const ShaderVariableManagerD3D12::ResourceAttribs& ShaderVariableManagerD3D12::GetResourceAttribs(uint32 Index) const
	{
		ASSERT_EXPR(m_pSignature != nullptr);
		return m_pSignature->GetResourceAttribs(Index);
	}


	ShaderVariableD3D12Impl* ShaderVariableManagerD3D12::GetVariable(const Char* Name) const
	{
		for (uint32 v = 0; v < m_NumVariables; ++v)
		{
			ShaderVariableD3D12Impl& Var = m_pVariables[v];
			if (strcmp(Var.GetDesc().Name, Name) == 0)
				return &Var;
		}
		return nullptr;
	}


	ShaderVariableD3D12Impl* ShaderVariableManagerD3D12::GetVariable(uint32 Index) const
	{
		if (Index >= m_NumVariables)
		{
			LOG_ERROR("Index ", Index, " is out of range");
			return nullptr;
		}

		return m_pVariables + Index;
	}

	uint32 ShaderVariableManagerD3D12::GetVariableIndex(const ShaderVariableD3D12Impl& Variable)
	{
		if (m_pVariables == nullptr)
		{
			LOG_ERROR("This shader variable manager has no variables");
			return ~0u;
		}

		const ptrdiff_t Offset = reinterpret_cast<const uint8*>(&Variable) - reinterpret_cast<uint8*>(m_pVariables);
		ASSERT(Offset % sizeof(ShaderVariableD3D12Impl) == 0, "Offset is not multiple of ShaderVariableD3D12Impl class size");
		uint32 Index = static_cast<uint32>(Offset / sizeof(ShaderVariableD3D12Impl));
		if (Index < m_NumVariables)
			return Index;
		else
		{
			LOG_ERROR("Failed to get variable index. The variable ", &Variable, " does not belong to this shader variable manager");
			return ~0u;
		}
	}

	void ShaderVariableManagerD3D12::BindResources(IResourceMapping* pResourceMapping, BIND_SHADER_RESOURCES_FLAGS Flags)
	{
		TBase::BindResources(pResourceMapping, Flags);
	}

	void ShaderVariableManagerD3D12::CheckResources(IResourceMapping* pResourceMapping,
		BIND_SHADER_RESOURCES_FLAGS          Flags,
		SHADER_RESOURCE_VARIABLE_TYPE_FLAGS& StaleVarTypes) const
	{
		TBase::CheckResources(pResourceMapping, Flags, StaleVarTypes);
	}

	namespace
	{

		class BindResourceHelper
		{
		public:
			BindResourceHelper(const PipelineResourceSignatureD3D12Impl& Signature,
				ShaderResourceCacheD3D12& ResourceCache,
				uint32                                    ResIndex,
				uint32                                    ArrayIndex,
				SET_SHADER_RESOURCE_FLAGS                 Flags);

			void operator()(const BindResourceInfo& BindInfo) const;

		private:
			
			void CacheCB(const BindResourceInfo& BindInfo) const;
			void CacheSampler(const BindResourceInfo& BindInfo) const;
			void CacheAccelStruct(const BindResourceInfo& BindInfo) const;
			

			void BindCombinedSampler(TextureViewD3D12Impl* pTexView, uint32 ArrayIndex, SET_SHADER_RESOURCE_FLAGS Flags) const;
			void BindCombinedSampler(BufferViewD3D12Impl* pTexView, uint32 ArrayIndex, SET_SHADER_RESOURCE_FLAGS Flags) const {}

			template <typename TResourceViewType, ///< The type of the view (TextureViewD3D12Impl or BufferViewD3D12Impl)
				typename TViewTypeEnum      ///< The type of the expected view type enum (TEXTURE_VIEW_TYPE or BUFFER_VIEW_TYPE)
			>
			void CacheResourceView(const BindResourceInfo& BindInfo,
				TViewTypeEnum           dbgExpectedViewType) const;

			ID3D12Device* GetD3D12Device() const { return m_Signature.GetDevice()->GetD3D12Device(); }

			void SetResource(D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle, RefCntAutoPtr<IDeviceObject>&& pObject) const
			{
				if (m_DstTableCPUDescriptorHandle.ptr != 0)
				{
					ASSERT(CPUDescriptorHandle.ptr != 0, "CPU descriptor handle must not be null for resources allocated in descriptor tables");
					ASSERT(m_DstRes.pObject == nullptr || m_AllowOverwrite,
						"Static and mutable resource descriptors should only be copied once unless ALLOW_OVERWRITE flag is set.");
					const D3D12_DESCRIPTOR_HEAP_TYPE d3d12HeapType = m_ResDesc.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER ?
						D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER :
						D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
					GetD3D12Device()->CopyDescriptorsSimple(1, m_DstTableCPUDescriptorHandle, CPUDescriptorHandle, d3d12HeapType);
				}

				m_ResourceCache.SetResource(m_RootIndex, m_OffsetFromTableStart,
					{
						m_ResDesc.ResourceType,
						CPUDescriptorHandle,
						std::move(pObject) //
					});
			}

		private:
			using ResourceAttribs = PipelineResourceSignatureD3D12Impl::ResourceAttribs;

			const PipelineResourceSignatureD3D12Impl& m_Signature;
			ShaderResourceCacheD3D12& m_ResourceCache;

			const PipelineResourceDesc& m_ResDesc;
			const ResourceAttribs& m_Attribs; // Must go before m_RootIndex, m_OffsetFromTableStart

			const ResourceCacheContentType m_CacheType; // Must go before m_RootIndex, m_OffsetFromTableStart
			const uint32                   m_RootIndex; // Must go before m_DstRes
			const uint32                   m_ArrayIndex;
			const uint32                   m_OffsetFromTableStart; // Must go before m_DstRes
			const bool                     m_AllowOverwrite;

			const ShaderResourceCacheD3D12::Resource& m_DstRes;

			D3D12_CPU_DESCRIPTOR_HANDLE m_DstTableCPUDescriptorHandle{};
		};

		BindResourceHelper::BindResourceHelper(
			const PipelineResourceSignatureD3D12Impl& Signature,
			ShaderResourceCacheD3D12& ResourceCache,
			uint32 ResIndex,
			uint32 ArrayIndex,
			SET_SHADER_RESOURCE_FLAGS Flags)
			: m_Signature{ Signature }
			, m_ResourceCache{ ResourceCache }
			, m_ResDesc{ Signature.GetResourceDesc(ResIndex) }
			, m_Attribs{ Signature.GetResourceAttribs(ResIndex) }
			, m_CacheType{ ResourceCache.GetContentType() }
			, m_RootIndex{ m_Attribs.RootIndex(m_CacheType) }
			, m_ArrayIndex{ ArrayIndex }
			, m_OffsetFromTableStart{ m_Attribs.OffsetFromTableStart(m_CacheType) + ArrayIndex }
			, m_AllowOverwrite{ m_ResDesc.VarType == SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC || (Flags & SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE) != 0 }
			, m_DstRes{ const_cast<const ShaderResourceCacheD3D12&>(ResourceCache).GetRootTable(m_RootIndex).GetResource(m_OffsetFromTableStart) }
		{
			ASSERT(ArrayIndex < m_ResDesc.ArraySize, "Array index is out of range, but it should've been corrected by ShaderVariableBase::SetArray()");

			if (m_CacheType != ResourceCacheContentType::Signature && !m_Attribs.IsRootView())
			{
				const bool                 IsSampler = (m_ResDesc.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER);
				const ROOT_PARAMETER_GROUP RootParamGroup = VariableTypeToRootParameterGroup(m_ResDesc.VarType);
				// Static/mutable resources are allocated in GPU-visible descriptor heap, while dynamic resources - in CPU-only heap.
				m_DstTableCPUDescriptorHandle =
					ResourceCache.GetDescriptorTableHandle<D3D12_CPU_DESCRIPTOR_HANDLE>(
						IsSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
						RootParamGroup, m_RootIndex, m_OffsetFromTableStart);
			}

#ifdef SHZ_DEBUG
			if (m_CacheType == ResourceCacheContentType::Signature)
			{
				ASSERT(m_DstTableCPUDescriptorHandle.ptr == 0, "Static shader resource cache should never be assigned descriptor space.");
			}
			else if (m_CacheType == ResourceCacheContentType::SRB)
			{
				if (m_Attribs.GetD3D12RootParamType() == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				{
					ASSERT(m_DstTableCPUDescriptorHandle.ptr != 0, "Shader resources allocated in descriptor tables must be assigned descriptor space.");
				}
				else
				{
					ASSERT_EXPR(m_Attribs.IsRootView());
					ASSERT((m_ResDesc.ResourceType == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER ||
						m_ResDesc.ResourceType == SHADER_RESOURCE_TYPE_BUFFER_SRV ||
						m_ResDesc.ResourceType == SHADER_RESOURCE_TYPE_BUFFER_UAV),
						"Only constant buffers and dynamic buffer views can be allocated as root views");
					ASSERT(m_DstTableCPUDescriptorHandle.ptr == 0, "Resources allocated as root views should never be assigned descriptor space.");
				}
			}
			else
			{
				ASSERT(false, "Unknown content type");
			}
#endif
		}

		void BindResourceHelper::CacheCB(const BindResourceInfo& BindInfo) const
		{
			ASSERT(BindInfo.pObject != nullptr, "Setting buffer to null is handled by BindResourceHelper::operator()");

			// We cannot use ClassPtrCast<> here as the resource can be of wrong type
			RefCntAutoPtr<BufferD3D12Impl> pBuffD3D12{ BindInfo.pObject, IID_BufferD3D12 };
#ifdef SHZ_DEBUG
			VerifyConstantBufferBinding(m_ResDesc, BindInfo, pBuffD3D12.RawPtr(), m_DstRes.pObject.RawPtr(),
				m_DstRes.BufferBaseOffset, m_DstRes.BufferRangeSize, m_Signature.GetDesc().Name);
			if (m_ResDesc.ArraySize != 1 && pBuffD3D12 && pBuffD3D12->GetDesc().Usage == USAGE_DYNAMIC && pBuffD3D12->GetD3D12Resource() == nullptr)
			{
				LOG_ERROR_MESSAGE("Attempting to bind dynamic buffer '", pBuffD3D12->GetDesc().Name, "' that doesn't have backing d3d12 resource to array variable '", m_ResDesc.Name,
					"[", m_ResDesc.ArraySize, "]', which is currently not supported in Direct3D12 backend. Either use non-array variable, or bind non-dynamic buffer.");
			}
#endif
			if (pBuffD3D12)
			{
				if (m_DstRes.pObject != nullptr && !m_AllowOverwrite)
				{
					// Do not update resource if one is already bound unless it is dynamic or ALLOW_OVERWRITE flag is set.
					// This may be dangerous as CopyDescriptorsSimple() may interfere with GPU reading the same descriptor.
					return;
				}

				D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle = pBuffD3D12->GetCBVHandle();
				ASSERT(CPUDescriptorHandle.ptr != 0 || pBuffD3D12->GetDesc().Usage == USAGE_DYNAMIC,
					"Only dynamic constant buffers may have null CPU descriptor");
				if (m_DstTableCPUDescriptorHandle.ptr != 0)
					ASSERT(CPUDescriptorHandle.ptr != 0, "CPU descriptor handle must not be null for resources allocated in descriptor tables");

				const BufferDesc& BuffDesc = pBuffD3D12->GetDesc();
				const uint64      RangeSize = BindInfo.BufferRangeSize == 0 ? BuffDesc.Size - BindInfo.BufferBaseOffset : BindInfo.BufferRangeSize;

				if (RangeSize != BuffDesc.Size)
				{
					// Default descriptor handle addresses the entire buffer, so we can't use it.
					// We will create a special CBV instead.
					// Note: special CBV is also created by ShaderResourceCacheD3D12::CopyResource().
					CPUDescriptorHandle.ptr = 0;
				}

				if (m_DstTableCPUDescriptorHandle.ptr != 0)
				{
					ASSERT(m_DstRes.pObject == nullptr || m_AllowOverwrite,
						"Static and mutable resource descriptors should only be copied once unless ALLOW_OVERWRITE flag is set.");
					if (RangeSize == BuffDesc.Size)
					{
						GetD3D12Device()->CopyDescriptorsSimple(1, m_DstTableCPUDescriptorHandle, CPUDescriptorHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					}
					else
					{
						pBuffD3D12->CreateCBV(m_DstTableCPUDescriptorHandle, BindInfo.BufferBaseOffset, RangeSize);
					}
				}

				m_ResourceCache.SetResource(m_RootIndex, m_OffsetFromTableStart,
					{
						m_ResDesc.ResourceType,
						CPUDescriptorHandle,
						std::move(pBuffD3D12),
						BindInfo.BufferBaseOffset,
						RangeSize //
					});
			}
		}


		void BindResourceHelper::CacheSampler(const BindResourceInfo& BindInfo) const
		{
			ASSERT(BindInfo.pObject != nullptr, "Setting sampler to null is handled by BindResourceHelper::operator()");

			RefCntAutoPtr<ISamplerD3D12> pSamplerD3D12{ BindInfo.pObject, IID_SamplerD3D12 };
#ifdef SHZ_DEBUG
			VerifySamplerBinding(m_ResDesc, BindInfo, pSamplerD3D12.RawPtr(), m_DstRes.pObject, m_Signature.GetDesc().Name);
#endif
			if (pSamplerD3D12)
			{
				if (m_DstRes.pObject != nullptr && !m_AllowOverwrite)
				{
					// Do not update resource if one is already bound unless it is dynamic or ALLOW_OVERWRITE flag is set.
					// This may be dangerous as CopyDescriptorsSimple() may interfere with GPU reading the same descriptor.
					return;
				}

				const D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle = pSamplerD3D12->GetCPUDescriptorHandle();
				ASSERT(CPUDescriptorHandle.ptr != 0, "Samplers must always have valid CPU descriptors");
				ASSERT(m_CacheType == ResourceCacheContentType::Signature || m_DstTableCPUDescriptorHandle.ptr != 0,
					"Samplers in SRB cache must always be allocated in root tables and thus assigned descriptor in the table");

				SetResource(CPUDescriptorHandle, std::move(pSamplerD3D12));
			}
		}


		void BindResourceHelper::CacheAccelStruct(const BindResourceInfo& BindInfo) const
		{
			ASSERT(BindInfo.pObject != nullptr, "Setting TLAS to null is handled by BindResourceHelper::operator()");

			RefCntAutoPtr<ITopLevelASD3D12> pTLASD3D12{ BindInfo.pObject, IID_TopLevelASD3D12 };
#ifdef SHZ_DEBUG
			VerifyTLASResourceBinding(m_ResDesc, BindInfo, pTLASD3D12.RawPtr(), m_DstRes.pObject.RawPtr(), m_Signature.GetDesc().Name);
#endif
			if (pTLASD3D12)
			{
				if (m_DstRes.pObject != nullptr && !m_AllowOverwrite)
				{
					// Do not update resource if one is already bound unless it is dynamic or ALLOW_OVERWRITE flag is set.
					// This may be dangerous as CopyDescriptorsSimple() may interfere with GPU reading the same descriptor.
					return;
				}

				const D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle = pTLASD3D12->GetCPUDescriptorHandle();
				ASSERT(CPUDescriptorHandle.ptr != 0, "Acceleration structures must always have valid CPU descriptor handles");
				ASSERT(m_CacheType == ResourceCacheContentType::Signature || m_DstTableCPUDescriptorHandle.ptr != 0,
					"Acceleration structures in SRB cache are always allocated in root tables and thus must have a descriptor");

				SetResource(CPUDescriptorHandle, std::move(pTLASD3D12));
			}
		}


		template <typename TResourceViewType>
		struct ResourceViewTraits
		{
		};

		template <>
		struct ResourceViewTraits<TextureViewD3D12Impl>
		{
			static const INTERFACE_ID& IID;

			static constexpr RESOURCE_DIMENSION ExpectedResDimension = RESOURCE_DIM_UNDEFINED;

			static bool VerifyView(const TextureViewD3D12Impl* pViewD3D12, const PipelineResourceDesc& ResDesc, uint32 ArrayIndex)
			{
				return true;
			}
		};
		const INTERFACE_ID& ResourceViewTraits<TextureViewD3D12Impl>::IID = IID_TextureViewD3D12;

		template <>
		struct ResourceViewTraits<BufferViewD3D12Impl>
		{
			static const INTERFACE_ID& IID;

			static constexpr RESOURCE_DIMENSION ExpectedResDimension = RESOURCE_DIM_BUFFER;

			static bool VerifyView(const BufferViewD3D12Impl* pViewD3D12, const PipelineResourceDesc& ResDesc, uint32 ArrayIndex)
			{
				if (pViewD3D12 != nullptr)
				{
					const BufferD3D12Impl* const pBuffer = pViewD3D12->GetBuffer<BufferD3D12Impl>();
					if (ResDesc.ArraySize != 1 && pBuffer->GetDesc().Usage == USAGE_DYNAMIC && pBuffer->GetD3D12Resource() == nullptr)
					{
						LOG_ERROR_MESSAGE("Attempting to bind dynamic buffer '", pBuffer->GetDesc().Name, "' that doesn't have backing d3d12 resource to array variable '", ResDesc.Name,
							"[", ResDesc.ArraySize, "]', which is currently not supported in Direct3D12 backend. Either use non-array variable, or bind non-dynamic buffer.");
						return false;
					}

					ValidateBufferMode(ResDesc, ArrayIndex, pViewD3D12);
				}

				return true;
			}
		};
		const INTERFACE_ID& ResourceViewTraits<BufferViewD3D12Impl>::IID = IID_BufferViewD3D12;

		template <typename TResourceViewType,
			typename TViewTypeEnum>
		void BindResourceHelper::CacheResourceView(const BindResourceInfo& BindInfo,
			TViewTypeEnum           dbgExpectedViewType) const
		{
			ASSERT(BindInfo.pObject != nullptr, "Setting resource view to null is handled by BindResourceHelper::operator()");

			// We cannot use ClassPtrCast<> here as the resource can be of wrong type
			RefCntAutoPtr<TResourceViewType> pViewD3D12{ BindInfo.pObject, ResourceViewTraits<TResourceViewType>::IID };
#ifdef SHZ_DEBUG
			VerifyResourceViewBinding(m_ResDesc, BindInfo, pViewD3D12.RawPtr(),
				{ dbgExpectedViewType },
				ResourceViewTraits<TResourceViewType>::ExpectedResDimension,
				false, // IsMultisample
				m_DstRes.pObject.RawPtr(),
				m_Signature.GetDesc().Name);
			ResourceViewTraits<TResourceViewType>::VerifyView(pViewD3D12, m_ResDesc, m_ArrayIndex);
#endif
			if (pViewD3D12)
			{
				if (m_DstRes.pObject != nullptr && !m_AllowOverwrite)
				{
					// Do not update resource if one is already bound unless it is dynamic or ALLOW_OVERWRITE flag is set.
					// This may be dangerous as CopyDescriptorsSimple() may interfere with GPU reading the same descriptor.
					return;
				}

				const D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle = pViewD3D12->GetCPUDescriptorHandle();
				// Note that for dynamic structured buffers we still create SRV even though we don't really use it.
				ASSERT(CPUDescriptorHandle.ptr != 0, "Texture/buffer views should always have valid CPU descriptor handles");

				BindCombinedSampler(pViewD3D12, BindInfo.ArrayIndex, BindInfo.Flags);

				SetResource(CPUDescriptorHandle, std::move(pViewD3D12));
			}
		}


		void BindResourceHelper::BindCombinedSampler(TextureViewD3D12Impl* pTexView, uint32 ArrayIndex, SET_SHADER_RESOURCE_FLAGS Flags) const
		{
			ASSERT_EXPR(pTexView != nullptr);

			if (m_ResDesc.ResourceType != SHADER_RESOURCE_TYPE_TEXTURE_SRV)
			{
				ASSERT(!m_Attribs.IsCombinedWithSampler(), "Only texture SRVs can be combined with sampler");
				return;
			}

			if (!m_Attribs.IsCombinedWithSampler())
				return;

			const PipelineResourceDesc& SamplerResDesc = m_Signature.GetResourceDesc(m_Attribs.SamplerInd);
			const ResourceAttribs& SamplerAttribs = m_Signature.GetResourceAttribs(m_Attribs.SamplerInd);
			ASSERT_EXPR(SamplerResDesc.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER);

			if (SamplerAttribs.IsImmutableSamplerAssigned())
			{
				// Immutable samplers should not be assigned cache space
				ASSERT_EXPR(SamplerAttribs.RootIndex(ResourceCacheContentType::Signature) == ResourceAttribs::InvalidSigRootIndex);
				ASSERT_EXPR(SamplerAttribs.RootIndex(ResourceCacheContentType::SRB) == ResourceAttribs::InvalidSRBRootIndex);
				ASSERT_EXPR(SamplerAttribs.SigOffsetFromTableStart == ResourceAttribs::InvalidOffset);
				ASSERT_EXPR(SamplerAttribs.SRBOffsetFromTableStart == ResourceAttribs::InvalidOffset);
				return;
			}

			ISampler* const pSampler = pTexView->GetSampler();
			if (pSampler == nullptr)
			{
				LOG_ERROR_MESSAGE("Failed to bind sampler to variable '", SamplerResDesc.Name, ". Sampler is not set in the texture view '", pTexView->GetDesc().Name, '\'');
				return;
			}

			ASSERT_EXPR(m_ResDesc.ArraySize == SamplerResDesc.ArraySize || SamplerResDesc.ArraySize == 1);
			const uint32 SamplerArrInd = SamplerResDesc.ArraySize > 1 ? ArrayIndex : 0;

			BindResourceHelper BindSampler{ m_Signature, m_ResourceCache, m_Attribs.SamplerInd, SamplerArrInd, Flags };
			BindSampler(BindResourceInfo{ SamplerArrInd, pSampler, Flags });
		}

		void BindResourceHelper::operator()(const BindResourceInfo& BindInfo) const
		{
			ASSERT_EXPR(m_ArrayIndex == BindInfo.ArrayIndex);
			if (BindInfo.pObject != nullptr)
			{
				static_assert(SHADER_RESOURCE_TYPE_LAST == 8, "Please update this function to handle the new resource type");
				switch (m_ResDesc.ResourceType)
				{
				case SHADER_RESOURCE_TYPE_CONSTANT_BUFFER:
					CacheCB(BindInfo);
					break;

				case SHADER_RESOURCE_TYPE_TEXTURE_SRV:
				case SHADER_RESOURCE_TYPE_INPUT_ATTACHMENT:
					CacheResourceView<TextureViewD3D12Impl>(BindInfo, TEXTURE_VIEW_SHADER_RESOURCE);
					break;

				case SHADER_RESOURCE_TYPE_TEXTURE_UAV:
					CacheResourceView<TextureViewD3D12Impl>(BindInfo, TEXTURE_VIEW_UNORDERED_ACCESS);
					break;

				case SHADER_RESOURCE_TYPE_BUFFER_SRV:
					CacheResourceView<BufferViewD3D12Impl>(BindInfo, BUFFER_VIEW_SHADER_RESOURCE);
					break;

				case SHADER_RESOURCE_TYPE_BUFFER_UAV:
					CacheResourceView<BufferViewD3D12Impl>(BindInfo, BUFFER_VIEW_UNORDERED_ACCESS);
					break;

				case SHADER_RESOURCE_TYPE_SAMPLER:
					CacheSampler(BindInfo);
					break;

				case SHADER_RESOURCE_TYPE_ACCEL_STRUCT:
					CacheAccelStruct(BindInfo);
					break;

				default: ASSERT(false, "Unknown resource type ", static_cast<int32>(m_ResDesc.ResourceType));
				}
			}
			else
			{
				ASSERT(m_DstRes.pObject == nullptr || m_AllowOverwrite,
					"Shader variable '", m_ResDesc.Name, "' is not dynamic, but is being reset to null. This is an error and may cause unpredicted behavior. ",
					"If this is intended and you ensured proper synchronization, use the SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE flag. "
					"Otherwise, use another shader resource binding instance or label the variable as dynamic.");

				m_ResourceCache.ResetResource(m_RootIndex, m_OffsetFromTableStart);
				if (m_Attribs.IsCombinedWithSampler())
				{
					const PipelineResourceDesc& SamplerResDesc = m_Signature.GetResourceDesc(m_Attribs.SamplerInd);
					const ResourceAttribs& SamplerAttribs = m_Signature.GetResourceAttribs(m_Attribs.SamplerInd);
					ASSERT_EXPR(SamplerResDesc.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER);

					if (!SamplerAttribs.IsImmutableSamplerAssigned())
					{
						const uint32 SamplerArrInd = SamplerResDesc.ArraySize > 1 ? m_ArrayIndex : 0;
						const uint32 SamRootIndex = SamplerAttribs.RootIndex(m_CacheType);
						const uint32 SamOffsetFromTableStart = SamplerAttribs.OffsetFromTableStart(m_CacheType) + SamplerArrInd;

						const ShaderResourceCacheD3D12::Resource& DstSam =
							const_cast<const ShaderResourceCacheD3D12&>(m_ResourceCache).GetRootTable(SamRootIndex).GetResource(SamOffsetFromTableStart);

						ASSERT(DstSam.pObject == nullptr || m_AllowOverwrite,
							"Sampler variable '", SamplerResDesc.Name, "' is not dynamic, but is being reset to null. This is an error and may cause unpredicted behavior. ",
							"Use another shader resource binding instance or label the variable as dynamic if you need to bind another sampler.");

						m_ResourceCache.ResetResource(SamRootIndex, SamOffsetFromTableStart);
					}
				}
			}
		}

	} // namespace


	void ShaderVariableManagerD3D12::BindResource(uint32 ResIndex, const BindResourceInfo& BindInfo)
	{
		ASSERT(m_pSignature->IsUsingSeparateSamplers() || GetResourceDesc(ResIndex).ResourceType != SHADER_RESOURCE_TYPE_SAMPLER,
			"Samplers should not be set directly when using combined texture samplers");
		BindResourceHelper BindResHelper{ *m_pSignature, m_ResourceCache, ResIndex, BindInfo.ArrayIndex, BindInfo.Flags };
		BindResHelper(BindInfo);
	}

	void ShaderVariableManagerD3D12::SetBufferDynamicOffset(uint32 ResIndex,
		uint32 ArrayIndex,
		uint32 BufferDynamicOffset)
	{
		const ResourceAttribs& Attribs = m_pSignature->GetResourceAttribs(ResIndex);
		const ResourceCacheContentType CacheType = m_ResourceCache.GetContentType();
		const uint32                   RootIndex = Attribs.RootIndex(CacheType);
		const uint32                   OffsetFromTableStart = Attribs.OffsetFromTableStart(CacheType) + ArrayIndex;

#ifdef SHZ_DEBUG
		{
			const PipelineResourceDesc& ResDesc = m_pSignature->GetResourceDesc(ResIndex);
			const ShaderResourceCacheD3D12::Resource& DstRes = const_cast<const ShaderResourceCacheD3D12&>(m_ResourceCache).GetRootTable(RootIndex).GetResource(OffsetFromTableStart);
			VerifyDynamicBufferOffset<BufferD3D12Impl, BufferViewD3D12Impl>(ResDesc, DstRes.pObject, DstRes.BufferBaseOffset, DstRes.BufferRangeSize, BufferDynamicOffset);
		}
#endif

		m_ResourceCache.SetBufferDynamicOffset(RootIndex, OffsetFromTableStart, BufferDynamicOffset);
	}

	IDeviceObject* ShaderVariableManagerD3D12::Get(uint32 ArrayIndex,
		uint32 ResIndex) const
	{
		const PipelineResourceDesc& ResDesc = GetResourceDesc(ResIndex);
		const ResourceAttribs& Attribs = GetResourceAttribs(ResIndex);
		const ResourceCacheContentType CacheType = m_ResourceCache.GetContentType();
		const uint32                   RootIndex = Attribs.RootIndex(CacheType);
		const uint32                   OffsetFromTableStart = Attribs.OffsetFromTableStart(CacheType) + ArrayIndex;

		ASSERT_EXPR(ArrayIndex < ResDesc.ArraySize);

		if (RootIndex < m_ResourceCache.GetNumRootTables())
		{
			const ShaderResourceCacheD3D12::RootTable& RootTable = const_cast<const ShaderResourceCacheD3D12&>(m_ResourceCache).GetRootTable(RootIndex);
			if (OffsetFromTableStart < RootTable.GetSize())
			{
				const ShaderResourceCacheD3D12::Resource& CachedRes = RootTable.GetResource(OffsetFromTableStart);
				return CachedRes.pObject;
			}
		}

		return nullptr;
	}

} // namespace shz
