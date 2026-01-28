#pragma once
#include "Engine/ECS/Public/Common.h"

namespace shz
{
	COMPONENT CBoxCollider final
	{
		Box Box;
		bool bIsSensor = false;

		uint64 ShapeHandle = 0;
	};
} // name