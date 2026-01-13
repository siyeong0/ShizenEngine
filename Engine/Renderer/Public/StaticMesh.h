#pragma once
#include <vector>
#include "Engine/Core/Math/Math.h"
#include "Engine/Renderer/Public/Material.h"

namespace shz
{
	enum INDEX_TYPE
	{
		INDEX_TYPE_UINT16,
		INDEX_TYPE_UINT32
	};

	struct MeshSection
	{
		RefCntAutoPtr<IBuffer> IndexBuffer;

		uint32_t NumIndices;
		uint32_t StartIndex;
		uint32_t BaseVertex;

		INDEX_TYPE IndexType = INDEX_TYPE_UINT32;

		MaterialHandle Material = {};
	};

	struct StaticMesh
	{
		RefCntAutoPtr<IBuffer> VertexBuffer;

		uint32_t NumVertices;
		uint32_t VertexStride;
		uint32_t VertexFormatId;

		std::vector<MeshSection> Sections;

		Box LocalBounds;
	};
} // namespace shz