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

#include <vector>

#include "TextureUploader.hpp"
#include "Engine/Core/Common/Public/ObjectBase.hpp"
#include "Engine/Core/Common/Public/HashUtils.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"

namespace std
{

	template <>
	struct hash<shz::UploadBufferDesc>
	{
		size_t operator()(const shz::UploadBufferDesc& Desc) const
		{
			return shz::ComputeHash(Desc.Width, Desc.Height, Desc.Depth, Desc.MipLevels, Desc.ArraySize, static_cast<shz::int32>(Desc.Format));
		}
	};

} // namespace std

namespace shz
{

	class UploadBufferBase : public ObjectBase<IUploadBuffer>
	{
	public:
		UploadBufferBase(
			IReferenceCounters* pRefCounters,
			const UploadBufferDesc& Desc,
			bool AllocateStagingData = false)
			: ObjectBase<IUploadBuffer>{ pRefCounters }
			, m_Desc{ Desc }
			, m_MappedData(size_t{ m_Desc.ArraySize } *size_t{ m_Desc.MipLevels })

		{
			if (AllocateStagingData)
			{
				TextureDesc StagingTexDesc;
				StagingTexDesc.Width = Desc.Width;
				StagingTexDesc.Height = Desc.Height;
				if (Desc.Depth > 1)
				{
					StagingTexDesc.Depth = Desc.Depth;
					ASSERT(Desc.ArraySize == 1, "3D textures cannot have array size greater than 1");
					StagingTexDesc.Type = RESOURCE_DIM_TEX_3D;
				}
				else if (Desc.ArraySize > 1)
				{
					StagingTexDesc.ArraySize = Desc.ArraySize;
					StagingTexDesc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
				}
				else
				{
					StagingTexDesc.Type = RESOURCE_DIM_TEX_2D;
				}
				StagingTexDesc.MipLevels = Desc.MipLevels;
				StagingTexDesc.Format = Desc.Format;

				constexpr uint32 Alignment = 4;
				const uint64     StagingTextureDataSize = GetStagingTextureDataSize(StagingTexDesc, Alignment);
				m_StagingData.resize(static_cast<size_t>(StagingTextureDataSize));

				for (uint32 Slice = 0; Slice < Desc.ArraySize; ++Slice)
				{
					for (uint32 Mip = 0; Mip < Desc.MipLevels; ++Mip)
					{
						const uint64             SubresOffset = GetStagingTextureSubresourceOffset(StagingTexDesc, Slice, Mip, Alignment);
						const MipLevelProperties MipProps = GetMipLevelProperties(StagingTexDesc, Mip);

						MappedTextureSubresource MappedData;
						MappedData.pData = &m_StagingData[static_cast<size_t>(SubresOffset)];
						MappedData.Stride = MipProps.RowSize;
						MappedData.DepthStride = MipProps.DepthSliceSize;
						SetMappedData(Mip, Slice, MappedData);
					}
				}
			}
		}

		virtual MappedTextureSubresource GetMappedData(uint32 Mip, uint32 Slice) override final
		{
			ASSERT_EXPR(Mip < m_Desc.MipLevels && Slice < m_Desc.ArraySize);
			return m_MappedData[size_t{ m_Desc.MipLevels } *size_t{ Slice } + size_t{ Mip }];
		}
		virtual const UploadBufferDesc& GetDesc() const override final { return m_Desc; }

		void SetMappedData(uint32 Mip, uint32 Slice, const MappedTextureSubresource& MappedData)
		{
			ASSERT_EXPR(Mip < m_Desc.MipLevels && Slice < m_Desc.ArraySize);
			m_MappedData[size_t{ m_Desc.MipLevels } *size_t{ Slice } + size_t{ Mip }] = MappedData;
		}

		bool IsMapped(uint32 Mip, uint32 Slice) const
		{
			ASSERT_EXPR(Mip < m_Desc.MipLevels && Slice < m_Desc.ArraySize);
			return m_MappedData[size_t{ m_Desc.MipLevels } *size_t{ Slice } + size_t{ Mip }].pData != nullptr;
		}

		void Reset()
		{
			if (!HasStagingData())
			{
				for (auto& MappedData : m_MappedData)
					MappedData = MappedTextureSubresource{};
			}
		}

		bool HasStagingData() const { return !m_StagingData.empty(); }

	protected:
		const UploadBufferDesc                m_Desc;
		std::vector<MappedTextureSubresource> m_MappedData;
		std::vector<uint8>                    m_StagingData;
	};

	class TextureUploaderBase : public ObjectBase<ITextureUploader>
	{
	public:
		TextureUploaderBase(IReferenceCounters* pRefCounters, IRenderDevice* pDevice, const TextureUploaderDesc& Desc)
			:ObjectBase<ITextureUploader>{ pRefCounters }
			, m_Desc{ Desc }
			, m_pDevice{ pDevice }
		{}

		template <typename UploadBufferType>
		struct PendingOperation
		{
			enum class Type
			{
				Map,
				Copy
			} OpType;

			bool AutoRecycle = false;

			RefCntAutoPtr<UploadBufferType> pUploadBuffer;
			RefCntAutoPtr<ITexture>         pDstTexture;

			uint32 DstSlice = 0;
			uint32 DstMip = 0;

			PendingOperation(Type Op, UploadBufferType* pBuff)
				: OpType{ Op }
				, pUploadBuffer{ pBuff }
			{
				ASSERT_EXPR(OpType == Type::Map);
			}

			PendingOperation(
				Type Op,
				UploadBufferType* pBuff,
				ITexture* pDstTex,
				uint32 dstSlice,
				uint32 dstMip,
				bool Recycle)
				: OpType{ Op }
				, AutoRecycle{ Recycle }
				, pUploadBuffer{ pBuff }
				, pDstTexture{ pDstTex }
				, DstSlice{ dstSlice }
				, DstMip{ dstMip }
			{
				ASSERT_EXPR(OpType == Type::Copy);
			}
		};

	protected:
		const TextureUploaderDesc    m_Desc;
		RefCntAutoPtr<IRenderDevice> m_pDevice;
	};

} // namespace shz
