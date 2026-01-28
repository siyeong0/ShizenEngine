#pragma once
#include "Engine/ECS/Public/Common.h"
#include <vector>

namespace shz
{
	COMPONENT CHeightFieldCollider final
	{
		uint32 Width = 0;
		uint32 Height = 0;
		float CellSizeX = 1.0f;
		float CellSizeZ = 1.0f;
		float HeightScale = 1.0f;
		float HeightOffset = 0.0f;
		std::vector<float> Heights = {};

		bool bIsSensor = false;

		uint64 ShapeHandle = 0;
	};
} // name