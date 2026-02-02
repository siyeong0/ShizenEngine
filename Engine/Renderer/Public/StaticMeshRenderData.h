#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/HashUtils.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RuntimeData/Public/Material.h"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/ISampler.h"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
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
			MaterialId MaterialId;

			Box LocalBounds = {};
		};
		std::vector<Section> Sections = {};

		StaticMeshRenderData() = default;
		StaticMeshRenderData(const StaticMeshRenderData&) = delete;
		StaticMeshRenderData(StaticMeshRenderData&&) = default;
		StaticMeshRenderData& operator=(const StaticMeshRenderData&) = delete;
		StaticMeshRenderData& operator=(StaticMeshRenderData&&) = default;
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
				s.MaterialId,
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


	DEFINE_HASH(shz::StaticMeshRenderData::Section);
	DEFINE_HASH(shz::StaticMeshRenderData);

#undef DEFINE_HASH
} // namespace std