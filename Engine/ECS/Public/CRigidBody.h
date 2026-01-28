#pragma once
#include "Engine/ECS/Public/Common.h"

namespace shz
{
	COMPONENT CRigidBody final
	{
		bool bValid = false;
		uint32 BodyId = 0;
		bool bDynamic = true;
	};
} // namespace shz
