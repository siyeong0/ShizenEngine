#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IBuffer.h"

#include "Engine/Renderer/Public/MaterialRenderData.h"

namespace shz
{
	using StaticMeshRenderDataId = uint64;

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
			MaterialRenderData Material;

			Box LocalBounds = {};
		};
		std::vector<Section> Sections = {};

		StaticMeshRenderData() = default;
		StaticMeshRenderData(const StaticMeshRenderData&) = delete;
		StaticMeshRenderData(StaticMeshRenderData&&) = default;
		StaticMeshRenderData& operator=(const StaticMeshRenderData&) = delete;
		StaticMeshRenderData& operator=(StaticMeshRenderData&&) = default;
	};
} // namespace std