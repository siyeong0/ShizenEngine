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

 // \file
 // Declaration of shz::PipelineResourceAttribsD3D12 struct

#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Public/PrivateConstants.h"
#include "Engine/RHI/Public/ShaderResourceCacheCommon.hpp"
#include "Primitives/DebugUtilities.hpp"
#include "Engine/Core/Common/Public/HashUtils.hpp"

namespace shz
{

	// sizeof(ResourceAttribs) == 16, x64
	struct PipelineResourceAttribsD3D12
	{
	private:
		static constexpr uint32 _RegisterBits = 16;
		static constexpr uint32 _SRBRootIndexBits = 16;
		static constexpr uint32 _SamplerIndBits = 16;
		static constexpr uint32 _SpaceBits = 8;
		static constexpr uint32 _SigRootIndexBits = 3;
		static constexpr uint32 _SamplerAssignedBits = 1;
		static constexpr uint32 _RootParamTypeBits = 4;


		static_assert((1u << _RegisterBits) >= MAX_RESOURCES_IN_SIGNATURE, "Not enough bits to store shader register");
		static_assert((1u << _SamplerIndBits) >= MAX_RESOURCES_IN_SIGNATURE, "Not enough bits to store sampler resource index");
		static_assert((1u << _RootParamTypeBits) > D3D12_ROOT_PARAMETER_TYPE_UAV + 1, "Not enough bits to store D3D12_ROOT_PARAMETER_TYPE");


	public:
		static constexpr uint32 InvalidSamplerInd = (1u << _SamplerIndBits) - 1;
		static constexpr uint32 InvalidSRBRootIndex = (1u << _SRBRootIndexBits) - 1;
		static constexpr uint32 InvalidSigRootIndex = (1u << _SigRootIndexBits) - 1;
		static constexpr uint32 InvalidRegister = (1u << _RegisterBits) - 1;
		static constexpr uint32 InvalidOffset = ~0u;


		/* 0  */const uint32  Register : _RegisterBits;        // Shader register
		/* 2  */const uint32  SRBRootIndex : _SRBRootIndexBits;    // Root view/table index in the SRB
		/* 4  */const uint32  SamplerInd : _SamplerIndBits;      // Assigned sampler index in m_Desc.Resources and m_pResourceAttribs
		/* 6  */const uint32  Space : _SpaceBits;           // Shader register space
		/* 7.0*/const uint32  SigRootIndex : _SigRootIndexBits;    // Root table index for signature (static resources only)
		/* 7.3*/const uint32  ImtblSamplerAssigned : _SamplerAssignedBits; // Immutable sampler flag for Texture SRVs and Samplers
		/* 7.4*/const uint32  RootParamType : _RootParamTypeBits;   // Root parameter type (D3D12_ROOT_PARAMETER_TYPE)
		/* 8  */const uint32  SigOffsetFromTableStart;                     // Offset in the root table for signature (static only)
		/* 12 */const uint32  SRBOffsetFromTableStart;                     // Offset in the root table for SRB
		/* 16 */


		PipelineResourceAttribsD3D12(
			uint32 _Register,
			uint32 _Space,
			uint32 _SamplerInd,
			uint32 _SRBRootIndex,
			uint32 _SRBOffsetFromTableStart,
			uint32 _SigRootIndex,
			uint32 _SigOffsetFromTableStart,
			bool _ImtblSamplerAssigned,
			D3D12_ROOT_PARAMETER_TYPE _RootParamType) noexcept
			: Register{ _Register }
			, SRBRootIndex{ _SRBRootIndex }
			, SamplerInd{ _SamplerInd }
			, Space{ _Space }
			, SigRootIndex{ _SigRootIndex }
			, ImtblSamplerAssigned{ _ImtblSamplerAssigned ? 1u : 0u }
			, RootParamType{ static_cast<uint32>(_RootParamType) }
			, SigOffsetFromTableStart{ _SigOffsetFromTableStart }
			, SRBOffsetFromTableStart{ _SRBOffsetFromTableStart }

		{
			ASSERT(Register == _Register, "Shader register (", _Register, ") exceeds maximum representable value");
			ASSERT(SRBRootIndex == _SRBRootIndex, "SRB Root index (", _SRBRootIndex, ") exceeds maximum representable value");
			ASSERT(SigRootIndex == _SigRootIndex, "Signature Root index (", _SigRootIndex, ") exceeds maximum representable value");
			ASSERT(SamplerInd == _SamplerInd, "Sampler index (", _SamplerInd, ") exceeds maximum representable value");
			ASSERT(Space == _Space, "Space (", _Space, ") exceeds maximum representable value");
			ASSERT(GetD3D12RootParamType() == _RootParamType, "Not enough bits to represent root parameter type");
		}

		// Only for serialization
		PipelineResourceAttribsD3D12() noexcept
			: PipelineResourceAttribsD3D12{ 0, 0, 0, 0, 0, 0, 0, false, D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE }
		{
		}

		bool IsImmutableSamplerAssigned() const
		{
			return ImtblSamplerAssigned != 0;
		}
		bool IsCombinedWithSampler() const
		{
			return SamplerInd != InvalidSamplerInd;
		}

		uint32 RootIndex(ResourceCacheContentType Type) const
		{
			return Type == ResourceCacheContentType::SRB ? SRBRootIndex : SigRootIndex;
		}
		uint32 OffsetFromTableStart(ResourceCacheContentType Type) const
		{
			return Type == ResourceCacheContentType::SRB ? SRBOffsetFromTableStart : SigOffsetFromTableStart;
		}

		D3D12_ROOT_PARAMETER_TYPE GetD3D12RootParamType() const { return static_cast<D3D12_ROOT_PARAMETER_TYPE>(RootParamType); }

		bool IsRootView() const
		{
			return (GetD3D12RootParamType() == D3D12_ROOT_PARAMETER_TYPE_CBV ||
				GetD3D12RootParamType() == D3D12_ROOT_PARAMETER_TYPE_SRV ||
				GetD3D12RootParamType() == D3D12_ROOT_PARAMETER_TYPE_UAV);
		}

		bool IsCompatibleWith(const PipelineResourceAttribsD3D12& rhs) const
		{
			// Ignore sampler index, signature root index & offset.

			return Register == rhs.Register &&
				Space == rhs.Space &&
				SRBRootIndex == rhs.SRBRootIndex &&
				SRBOffsetFromTableStart == rhs.SRBOffsetFromTableStart &&
				ImtblSamplerAssigned == rhs.ImtblSamplerAssigned &&
				RootParamType == rhs.RootParamType;

		}

		size_t GetHash() const
		{
			return ComputeHash(Register, Space, SRBRootIndex, SRBOffsetFromTableStart, ImtblSamplerAssigned, RootParamType);
		}
	};
	ASSERT_SIZEOF(PipelineResourceAttribsD3D12, 16, "The struct is used in serialization and must be tightly packed");

} // namespace shz
