#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/HashUtils.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/Material.h"

#include "Engine/RHI/Interface/IBuffer.h"

namespace shz
{
	struct StaticMeshLevelRenderData final
	{
		RefCntAutoPtr<IBuffer> VertexBuffer = {};
		RefCntAutoPtr<IBuffer> IndexBuffer = {};

		uint32 VertexStride = 0;
		uint32 VertexCount = 0;
		uint32 IndexCount = 0;
		VALUE_TYPE IndexType = VT_UINT32;

		StaticMeshBounds LocalBounds = {};

		struct Section final
		{
			uint32 FirstIndex = 0;
			uint32 IndexCount = 0;
			uint32 BaseVertex = 0;
			MaterialId MaterialId;

			StaticMeshBounds LocalBounds = {};
		};
		std::vector<Section> Sections = {};

		StaticMeshLevelRenderData() = default;
		StaticMeshLevelRenderData(const StaticMeshLevelRenderData&) = delete;
		StaticMeshLevelRenderData(StaticMeshLevelRenderData&&) = default;
		StaticMeshLevelRenderData& operator=(const StaticMeshLevelRenderData&) = delete;
		StaticMeshLevelRenderData& operator=(StaticMeshLevelRenderData&&) = default;
	};

	struct StaticMeshRenderData final
	{
		std::vector<StaticMeshLevelRenderData> Levels = {};
		std::vector<float> LODScreenSizes = {};

		std::vector<StaticMeshLevelRenderData>& GetLevels() noexcept { return Levels; }
		const std::vector<StaticMeshLevelRenderData>& GetLevels() const noexcept { return Levels; }

		StaticMeshLevelRenderData& GetLevel(uint32 lod) noexcept { return Levels[lod]; }
		const StaticMeshLevelRenderData& GetLevel(uint32 lod) const noexcept { return Levels[lod]; }

		uint32 GetLevelCount() const noexcept { return static_cast<uint32>(Levels.size()); }

		StaticMeshLevelRenderData& operator[](size_t idx) { return Levels[idx]; }
		const StaticMeshLevelRenderData& operator[](size_t idx) const { return Levels[idx]; }

		const StaticMeshBounds& GetLocalBounds() const { return Levels[0].LocalBounds; }
	};

	// ------------------------------------------------------------
	// Hashing support
	// ------------------------------------------------------------

	template <typename HasherType>
	struct HashCombiner<HasherType, StaticMeshLevelRenderData::Section> : HashCombinerBase<HasherType>
	{
		HashCombiner(HasherType& Hasher)
			: HashCombinerBase<HasherType>{ Hasher }
		{}

		void operator()(const StaticMeshLevelRenderData::Section& s) const
		{
			this->m_Hasher(
				s.FirstIndex,
				s.IndexCount,
				s.BaseVertex,
				s.MaterialId);
		}
	};

	template <typename HasherType>
	struct HashCombiner<HasherType, StaticMeshLevelRenderData> : HashCombinerBase<HasherType>
	{
		HashCombiner(HasherType& Hasher)
			: HashCombinerBase<HasherType>{ Hasher }
		{}

		void operator()(const StaticMeshLevelRenderData& v) const
		{
			this->m_Hasher(
				v.VertexBuffer,
				v.IndexBuffer,
				v.VertexStride,
				v.VertexCount,
				v.IndexCount,
				v.IndexType);

			// Sections (order-sensitive)
			this->m_Hasher(v.Sections.size());
			for (const auto& sec : v.Sections)
			{
				this->m_Hasher(sec);
			}
		}
	};

	template <typename HasherType>
	struct HashCombiner < HasherType, StaticMeshRenderData> : HashCombinerBase<HasherType>
	{
		HashCombiner(HasherType& Hasher)
			: HashCombinerBase<HasherType>{ Hasher }
		{}
		void operator()(const StaticMeshRenderData& v) const
		{
			this->m_Hasher(v.Levels.size());
			for (const auto& lvl : v.Levels)
			{
				this->m_Hasher(lvl);
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


	DEFINE_HASH(shz::StaticMeshLevelRenderData::Section);
	DEFINE_HASH(shz::StaticMeshLevelRenderData);
	DEFINE_HASH(shz::StaticMeshRenderData);

#undef DEFINE_HASH
} // namespace std