#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/HashUtils.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/ISampler.h"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	struct TextureRenderData final
	{
		RefCntAutoPtr<ITexture> Texture = {};
		RefCntAutoPtr<ISampler> Sampler = {};
	};

	struct MaterialRenderData final
	{
		RefCntAutoPtr<IPipelineState> PSO = {};
		RefCntAutoPtr<IShaderResourceBinding>  SRB = {};

		RefCntAutoPtr<IBuffer> ConstantBuffer = {};
		uint32 CBIndex = 0;
		std::vector<TextureRenderData> BoundTextures = {};

		RefCntAutoPtr<IShaderResourceBinding>  ShadowSRB = {};
	};

	struct StaticMeshRenderData final
	{
		RefCntAutoPtr<IBuffer> VertexBuffer = {};
		RefCntAutoPtr<IBuffer> IndexBuffer = {};

		uint32 VertexStride = 0;
		uint32 VertexCount = 0;
		uint32 IndexCount = 0;
		VALUE_TYPE IndexType = VT_UINT32;

		Box LocalBounds = {};

		struct Section final
		{
			uint32 FirstIndex = 0;
			uint32 IndexCount = 0;
			uint32 BaseVertex = 0;
			MaterialRenderData Material = {};

			Box LocalBounds = {};
		};
		std::vector<Section> Sections = {};
	};


	// ------------------------------------------------------------
	// Hash combiners for RenderData types
	// ------------------------------------------------------------

	template <typename HasherType>
	struct HashCombiner<HasherType, TextureRenderData> : HashCombinerBase<HasherType>
	{
		HashCombiner(HasherType& Hasher)
			: HashCombinerBase<HasherType>{ Hasher }
		{}

		void operator()(const TextureRenderData& v) const
		{
			this->m_Hasher(v.Texture, v.Sampler);
		}
	};

	template <typename HasherType>
	struct HashCombiner<HasherType, MaterialRenderData> : HashCombinerBase<HasherType>
	{
		HashCombiner(HasherType& Hasher)
			: HashCombinerBase<HasherType>{ Hasher }
		{}

		void operator()(const MaterialRenderData& v) const
		{
			this->m_Hasher(
				v.PSO,
				v.SRB,
				v.ConstantBuffer,
				v.CBIndex,
				v.ShadowSRB);

			// BoundTextures (order-sensitive)
			this->m_Hasher(v.BoundTextures.size());
			for (const auto& tex : v.BoundTextures)
			{
				this->m_Hasher(tex);
			}
		}
	};

	template <typename HasherType>
	struct HashCombiner<HasherType, StaticMeshRenderData::Section> : HashCombinerBase<HasherType>
	{
		HashCombiner(HasherType& Hasher)
			: HashCombinerBase<HasherType>{ Hasher }
		{}

		void operator()(const StaticMeshRenderData::Section& s) const
		{
			this->m_Hasher(
				s.FirstIndex,
				s.IndexCount,
				s.BaseVertex,
				s.Material,
				s.LocalBounds);
		}
	};

	template <typename HasherType>
	struct HashCombiner<HasherType, StaticMeshRenderData> : HashCombinerBase<HasherType>
	{
		HashCombiner(HasherType& Hasher)
			: HashCombinerBase<HasherType>{ Hasher }
		{}

		void operator()(const StaticMeshRenderData& v) const
		{
			this->m_Hasher(
				v.VertexBuffer,
				v.IndexBuffer,
				v.VertexStride,
				v.VertexCount,
				v.IndexCount,
				v.IndexType,
				v.LocalBounds);

			// Sections (order-sensitive)
			this->m_Hasher(v.Sections.size());
			for (const auto& sec : v.Sections)
			{
				this->m_Hasher(sec);
			}
		}
	};

}

namespace std
{
#define DEFINE_HASH(Type)                        \
    template <>                                  \
    struct hash<Type>                            \
    {                                            \
        size_t operator()(const Type& Val) const \
        {                                        \
            shz::StdHasher<Type> Hasher;		 \
            return Hasher(Val);                  \
        }                                        \
    }


	DEFINE_HASH(shz::TextureRenderData);
	DEFINE_HASH(shz::MaterialRenderData);
	DEFINE_HASH(shz::StaticMeshRenderData::Section);
	DEFINE_HASH(shz::StaticMeshRenderData);

#undef DEFINE_HASH
} // namespace std