#pragma once
#include <vector>
#include "Engine/Core/Math/Math.h"

namespace shz
{
	struct MeshSection
	{
		RefCntAutoPtr<IBuffer> IndexBuffer;

		uint32_t NumIndices;
		uint32_t StartIndex;

		VALUE_TYPE IndexType = VT_UINT32;

		MaterialHandle Material = {};

		Box LocalBounds;
	};

	struct StaticMeshRenderData
	{
		RefCntAutoPtr<IBuffer> VertexBuffer;

		uint32_t NumVertices;
		uint32_t VertexStride;
		uint32_t VertexFormatId;

		std::vector<MeshSection> Sections;

		Box LocalBounds;
	};
} // namespace shz