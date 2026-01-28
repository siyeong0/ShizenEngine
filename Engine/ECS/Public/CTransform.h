#pragma once
#include "Engine/ECS/Public/Common.h"

namespace shz
{
	// World transform (Euler rotation in radians: {Pitch, Yaw, Roll} or your convention)
	COMPONENT CTransform final
	{
		float3 Position = { 0, 0, 0 };
		float3 Rotation = { 0, 0, 0 };
		float3 Scale = { 1, 1, 1 };
	};
} // namespace shz
