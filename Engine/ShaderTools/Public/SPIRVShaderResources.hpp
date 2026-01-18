/*
 *  Copyright 2019-2024 Diligent Graphics LLC
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
 // Declaration of shz::SPIRVShaderResources class

 // SPIRVShaderResources class uses continuous chunk of memory to store all resources, as follows:
 //
 //   m_MemoryBuffer                                                                                                              m_TotalResources
 //    |                                                                                                                             |                                       |
 //    | Uniform Buffers | Storage Buffers | Storage Images | Sampled Images | Atomic Counters | Separate Samplers | Separate Images |   Stage Inputs   |   Resource Names   |

#include <memory>
#include <vector>
#include <sstream>
#include <array>

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineResourceSignature.h"
#include "Engine/Core/Memory/Public/STDAllocator.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Common/Public/StringPool.hpp"

#ifdef SHZSPIRV_CROSS_NAMESPACE
#    define shizen_spirv_cross SHZSPIRV_CROSS_NAMESPACE
#else
#    define shizen_spirv_cross spirv_cross
#endif

namespace shizen_spirv_cross
{
	class Compiler;
	struct Resource;
} // namespace shz_spirv_cross

namespace shz
{

	// sizeof(SPIRVShaderResourceAttribs) == 32, msvc x64
	struct SPIRVShaderResourceAttribs
	{
		enum ResourceType : uint8
		{
			UniformBuffer = 0,
			ROStorageBuffer,
			RWStorageBuffer,
			UniformTexelBuffer,
			StorageTexelBuffer,
			StorageImage,
			SampledImage,
			AtomicCounter,
			SeparateImage,
			SeparateSampler,
			InputAttachment,
			AccelerationStructure,
			NumResourceTypes
		};

		static SHADER_RESOURCE_TYPE    GetShaderResourceType(ResourceType Type);
		static PIPELINE_RESOURCE_FLAGS GetPipelineResourceFlags(ResourceType Type);

		

		/*  0  */const char* const      Name;
		/*  8  */const uint16           ArraySize;
		/* 10  */const ResourceType     Type;
		/* 11.0*/const uint8            ResourceDim : 7; // RESOURCE_DIMENSION
		/* 11.7*/const uint8            IsMS : 1;

		// Offset in SPIRV words (uint32_t) of binding & descriptor set decorations in SPIRV binary
		/* 12 */const uint32_t          BindingDecorationOffset;
		/* 16 */const uint32_t          DescriptorSetDecorationOffset;

		/* 20 */const uint32            BufferStaticSize;
		/* 24 */const uint32            BufferStride;
		/* 28 */
		/* 32 */ // End of structure

			

		SPIRVShaderResourceAttribs(
			const shizen_spirv_cross::Compiler& Compiler,
			const shizen_spirv_cross::Resource& Res,
			const char* _Name,
			ResourceType                          _Type,
			uint32                                _BufferStaticSize = 0,
			uint32                                _BufferStride = 0) noexcept;

		ShaderResourceDesc GetResourceDesc() const
		{
			return ShaderResourceDesc{ Name, GetShaderResourceType(Type), ArraySize };
		}

		RESOURCE_DIMENSION GetResourceDimension() const
		{
			return static_cast<RESOURCE_DIMENSION>(ResourceDim);
		}

		bool IsMultisample() const
		{
			return IsMS != 0;
		}
	};
	static_assert(sizeof(SPIRVShaderResourceAttribs) % sizeof(void*) == 0, "Size of SPIRVShaderResourceAttribs struct must be multiple of sizeof(void*)");

	// sizeof(SPIRVShaderResourceAttribs) == 16, msvc x64
	struct SPIRVShaderStageInputAttribs
	{
		
		SPIRVShaderStageInputAttribs(const char* _Semantic, uint32_t _LocationDecorationOffset)
			: Semantic{ _Semantic }
			, LocationDecorationOffset{ _LocationDecorationOffset }
		{
		}
		

		const char* const Semantic;
		const uint32_t    LocationDecorationOffset;
	};
	static_assert(sizeof(SPIRVShaderStageInputAttribs) % sizeof(void*) == 0, "Size of SPIRVShaderStageInputAttribs struct must be multiple of sizeof(void*)");

	// shz::SPIRVShaderResources class
	class SPIRVShaderResources
	{
	public:
		SPIRVShaderResources(IMemoryAllocator& Allocator,
			std::vector<uint32_t> spirv_binary,
			const ShaderDesc& shaderDesc,
			const char* CombinedSamplerSuffix,
			bool                  LoadShaderStageInputs,
			bool                  LoadUniformBufferReflection,
			std::string& EntryPoint) noexcept(false);

		
		SPIRVShaderResources(const SPIRVShaderResources&) = delete;
		SPIRVShaderResources(SPIRVShaderResources&&) = delete;
		SPIRVShaderResources& operator = (const SPIRVShaderResources&) = delete;
		SPIRVShaderResources& operator = (SPIRVShaderResources&&) = delete;
		

		~SPIRVShaderResources();

		

		uint32 GetNumUBs()const noexcept { return (m_StorageBufferOffset - 0); }
		uint32 GetNumSBs()const noexcept { return (m_StorageImageOffset - m_StorageBufferOffset); }
		uint32 GetNumImgs()const noexcept { return (m_SampledImageOffset - m_StorageImageOffset); }
		uint32 GetNumSmpldImgs()const noexcept { return (m_AtomicCounterOffset - m_SampledImageOffset); }
		uint32 GetNumACs()const noexcept { return (m_SeparateSamplerOffset - m_AtomicCounterOffset); }
		uint32 GetNumSepSmplrs()const noexcept { return (m_SeparateImageOffset - m_SeparateSamplerOffset); }
		uint32 GetNumSepImgs()const noexcept { return (m_InputAttachmentOffset - m_SeparateImageOffset); }
		uint32 GetNumInptAtts()const noexcept { return (m_AccelStructOffset - m_InputAttachmentOffset); }
		uint32 GetNumAccelStructs()const noexcept { return (m_TotalResources - m_AccelStructOffset); }
		uint32 GetTotalResources()    const noexcept { return m_TotalResources; }
		uint32 GetNumShaderStageInputs()const noexcept { return m_NumShaderStageInputs; }

		const SPIRVShaderResourceAttribs& GetUB(uint32 n)const noexcept { return GetResAttribs(n, GetNumUBs(), 0); }
		const SPIRVShaderResourceAttribs& GetSB(uint32 n)const noexcept { return GetResAttribs(n, GetNumSBs(), m_StorageBufferOffset); }
		const SPIRVShaderResourceAttribs& GetImg(uint32 n)const noexcept { return GetResAttribs(n, GetNumImgs(), m_StorageImageOffset); }
		const SPIRVShaderResourceAttribs& GetSmpldImg(uint32 n)const noexcept { return GetResAttribs(n, GetNumSmpldImgs(), m_SampledImageOffset); }
		const SPIRVShaderResourceAttribs& GetAC(uint32 n)const noexcept { return GetResAttribs(n, GetNumACs(), m_AtomicCounterOffset); }
		const SPIRVShaderResourceAttribs& GetSepSmplr(uint32 n)const noexcept { return GetResAttribs(n, GetNumSepSmplrs(), m_SeparateSamplerOffset); }
		const SPIRVShaderResourceAttribs& GetSepImg(uint32 n)const noexcept { return GetResAttribs(n, GetNumSepImgs(), m_SeparateImageOffset); }
		const SPIRVShaderResourceAttribs& GetInptAtt(uint32 n)const noexcept { return GetResAttribs(n, GetNumInptAtts(), m_InputAttachmentOffset); }
		const SPIRVShaderResourceAttribs& GetAccelStruct(uint32 n)const noexcept { return GetResAttribs(n, GetNumAccelStructs(), m_AccelStructOffset); }
		const SPIRVShaderResourceAttribs& GetResource(uint32 n)const noexcept { return GetResAttribs(n, GetTotalResources(), 0); }

		

		const SPIRVShaderStageInputAttribs& GetShaderStageInputAttribs(uint32 n) const noexcept
		{
			ASSERT(n < m_NumShaderStageInputs, "Shader stage input index (", n, ") is out of range. Total input count: ", m_NumShaderStageInputs);
			const SPIRVShaderResourceAttribs* ResourceMemoryEnd = reinterpret_cast<const SPIRVShaderResourceAttribs*>(m_MemoryBuffer.get()) + m_TotalResources;
			return reinterpret_cast<const SPIRVShaderStageInputAttribs*>(ResourceMemoryEnd)[n];
		}

		const ShaderCodeBufferDesc* GetUniformBufferDesc(uint32 Index) const
		{
			if (Index >= GetNumUBs())
			{
				ASSERT(false, "Uniform buffer index (", Index, ") is out of range.");
				return nullptr;
			}

			if (!m_UBReflectionBuffer)
			{
				ASSERT(false, "Uniform buffer reflection information is not loaded. Please set the LoadConstantBufferReflection flag when creating the shader.");
				return nullptr;
			}

			return reinterpret_cast<const ShaderCodeBufferDesc*>(m_UBReflectionBuffer.get()) + Index;
		}

		struct ResourceCounters
		{
			uint32 NumUBs = 0;
			uint32 NumSBs = 0;
			uint32 NumImgs = 0;
			uint32 NumSmpldImgs = 0;
			uint32 NumACs = 0;
			uint32 NumSepSmplrs = 0;
			uint32 NumSepImgs = 0;
			uint32 NumInptAtts = 0;
			uint32 NumAccelStructs = 0;
		};

		SHADER_TYPE GetShaderType() const noexcept { return m_ShaderType; }

		const std::array<uint32, 3>& GetComputeGroupSize() const
		{
			return m_ComputeGroupSize;
		}

		template <typename THandleUB,
			typename THandleSB,
			typename THandleImg,
			typename THandleSmplImg,
			typename THandleAC,
			typename THandleSepSmpl,
			typename THandleSepImg,
			typename THandleInptAtt,
			typename THandleAccelStruct>
		void ProcessResources(THandleUB          HandleUB,
			THandleSB          HandleSB,
			THandleImg         HandleImg,
			THandleSmplImg     HandleSmplImg,
			THandleAC          HandleAC,
			THandleSepSmpl     HandleSepSmpl,
			THandleSepImg      HandleSepImg,
			THandleInptAtt     HandleInptAtt,
			THandleAccelStruct HandleAccelStruct) const
		{
			for (uint32 n = 0; n < GetNumUBs(); ++n)
			{
				const SPIRVShaderResourceAttribs& UB = GetUB(n);
				HandleUB(UB, n);
			}

			for (uint32 n = 0; n < GetNumSBs(); ++n)
			{
				const SPIRVShaderResourceAttribs& SB = GetSB(n);
				HandleSB(SB, n);
			}

			for (uint32 n = 0; n < GetNumImgs(); ++n)
			{
				const SPIRVShaderResourceAttribs& Img = GetImg(n);
				HandleImg(Img, n);
			}

			for (uint32 n = 0; n < GetNumSmpldImgs(); ++n)
			{
				const SPIRVShaderResourceAttribs& SmplImg = GetSmpldImg(n);
				HandleSmplImg(SmplImg, n);
			}

			for (uint32 n = 0; n < GetNumACs(); ++n)
			{
				const SPIRVShaderResourceAttribs& AC = GetAC(n);
				HandleAC(AC, n);
			}

			for (uint32 n = 0; n < GetNumSepSmplrs(); ++n)
			{
				const SPIRVShaderResourceAttribs& SepSmpl = GetSepSmplr(n);
				HandleSepSmpl(SepSmpl, n);
			}

			for (uint32 n = 0; n < GetNumSepImgs(); ++n)
			{
				const SPIRVShaderResourceAttribs& SepImg = GetSepImg(n);
				HandleSepImg(SepImg, n);
			}

			for (uint32 n = 0; n < GetNumInptAtts(); ++n)
			{
				const SPIRVShaderResourceAttribs& InptAtt = GetInptAtt(n);
				HandleInptAtt(InptAtt, n);
			}

			for (uint32 n = 0; n < GetNumAccelStructs(); ++n)
			{
				const SPIRVShaderResourceAttribs& AccelStruct = GetAccelStruct(n);
				HandleAccelStruct(AccelStruct, n);
			}

			static_assert(uint32{ SPIRVShaderResourceAttribs::ResourceType::NumResourceTypes } == 12, "Please handle the new resource type here, if needed");
		}

		template <typename THandler>
		void ProcessResources(THandler&& Handler) const
		{
			for (uint32 n = 0; n < GetTotalResources(); ++n)
			{
				const SPIRVShaderResourceAttribs& Res = GetResource(n);
				Handler(Res, n);
			}
		}

		std::string DumpResources() const;

		

		const char* GetCombinedSamplerSuffix() const { return m_CombinedSamplerSuffix; }
		const char* GetShaderName()            const { return m_ShaderName; }
		bool        IsUsingCombinedSamplers()  const { return m_CombinedSamplerSuffix != nullptr; }

		

		bool IsHLSLSource() const { return m_IsHLSLSource; }

		// Sets the input location decorations using the HLSL semantic names.
		void MapHLSLVertexShaderInputs(std::vector<uint32_t>& SPIRV) const;

	private:
		void Initialize(IMemoryAllocator& Allocator,
			const ResourceCounters& Counters,
			uint32                  NumShaderStageInputs,
			size_t                  ResourceNamesPoolSize,
			StringPool& ResourceNamesPool);

		SPIRVShaderResourceAttribs& GetResAttribs(uint32 n, uint32 NumResources, uint32 Offset) noexcept
		{
			ASSERT(n < NumResources, "Resource index (", n, ") is out of range. Total resource count: ", NumResources);
			ASSERT_EXPR(Offset + n < m_TotalResources);
			return reinterpret_cast<SPIRVShaderResourceAttribs*>(m_MemoryBuffer.get())[Offset + n];
		}

		const SPIRVShaderResourceAttribs& GetResAttribs(uint32 n, uint32 NumResources, uint32 Offset) const noexcept
		{
			ASSERT(n < NumResources, "Resource index (", n, ") is out of range. Total resource count: ", NumResources);
			ASSERT_EXPR(Offset + n < m_TotalResources);
			return reinterpret_cast<SPIRVShaderResourceAttribs*>(m_MemoryBuffer.get())[Offset + n];
		}

		

		SPIRVShaderResourceAttribs& GetUB(uint32 n)noexcept { return GetResAttribs(n, GetNumUBs(), 0); }
		SPIRVShaderResourceAttribs& GetSB(uint32 n)noexcept { return GetResAttribs(n, GetNumSBs(), m_StorageBufferOffset); }
		SPIRVShaderResourceAttribs& GetImg(uint32 n)noexcept { return GetResAttribs(n, GetNumImgs(), m_StorageImageOffset); }
		SPIRVShaderResourceAttribs& GetSmpldImg(uint32 n)noexcept { return GetResAttribs(n, GetNumSmpldImgs(), m_SampledImageOffset); }
		SPIRVShaderResourceAttribs& GetAC(uint32 n)noexcept { return GetResAttribs(n, GetNumACs(), m_AtomicCounterOffset); }
		SPIRVShaderResourceAttribs& GetSepSmplr(uint32 n)noexcept { return GetResAttribs(n, GetNumSepSmplrs(), m_SeparateSamplerOffset); }
		SPIRVShaderResourceAttribs& GetSepImg(uint32 n)noexcept { return GetResAttribs(n, GetNumSepImgs(), m_SeparateImageOffset); }
		SPIRVShaderResourceAttribs& GetInptAtt(uint32 n)noexcept { return GetResAttribs(n, GetNumInptAtts(), m_InputAttachmentOffset); }
		SPIRVShaderResourceAttribs& GetAccelStruct(uint32 n)noexcept { return GetResAttribs(n, GetNumAccelStructs(), m_AccelStructOffset); }
		SPIRVShaderResourceAttribs& GetResource(uint32 n)noexcept { return GetResAttribs(n, GetTotalResources(), 0); }

		

		SPIRVShaderStageInputAttribs& GetShaderStageInputAttribs(uint32 n) noexcept
		{
			return const_cast<SPIRVShaderStageInputAttribs&>(const_cast<const SPIRVShaderResources*>(this)->GetShaderStageInputAttribs(n));
		}

		// Memory buffer that holds all resources as continuous chunk of memory:
		// |  UBs  |  SBs  |  StrgImgs  |  SmplImgs  |  ACs  |  SepSamplers  |  SepImgs  | Stage Inputs | Resource Names |
		std::unique_ptr<void, STDDeleterRawMem<void>> m_MemoryBuffer;
		std::unique_ptr<void, STDDeleterRawMem<void>> m_UBReflectionBuffer;

		const char* m_CombinedSamplerSuffix = nullptr;
		const char* m_ShaderName = nullptr;

		using OffsetType = uint16;
		OffsetType m_StorageBufferOffset = 0;
		OffsetType m_StorageImageOffset = 0;
		OffsetType m_SampledImageOffset = 0;
		OffsetType m_AtomicCounterOffset = 0;
		OffsetType m_SeparateSamplerOffset = 0;
		OffsetType m_SeparateImageOffset = 0;
		OffsetType m_InputAttachmentOffset = 0;
		OffsetType m_AccelStructOffset = 0;
		OffsetType m_TotalResources = 0;
		OffsetType m_NumShaderStageInputs = 0;

		SHADER_TYPE m_ShaderType = SHADER_TYPE_UNKNOWN;

		std::array<uint32, 3> m_ComputeGroupSize = {};

		// Indicates if the shader was compiled from HLSL source.
		bool m_IsHLSLSource = false;
	};

} // namespace shz
