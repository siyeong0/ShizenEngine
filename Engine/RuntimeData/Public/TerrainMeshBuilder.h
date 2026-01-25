#pragma once

#include <cstdint>
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/RuntimeData/Public/TerrainHeightField.h"
#include "Engine/RuntimeData/Public/StaticMesh.h"

namespace shz
{
	struct TerrainMeshBuildSettings final
	{
		bool bGenerateTexCoords = true;
		bool bGenerateNormals = true;
		bool bPreferU16Indices = true;
		bool bFlipWinding = false;
		bool bCenterXZ = true;
		float YOffset = 0.f;
		float NormalUpBias = 2.f;
	};

	class TerrainMeshBuilder final
	{
	public:
		// Builds a single StaticMesh from the entire heightfield.
		// One section, one material slot.
		static bool BuildStaticMesh(
			StaticMesh* pOutMesh,
			const TerrainHeightField& hf,
			Material&& terrainMaterial,
			const TerrainMeshBuildSettings& settings = {});
	};
} // namespace shz
