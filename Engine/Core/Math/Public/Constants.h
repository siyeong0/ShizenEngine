#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
	constexpr float32 PI = 3.14159265358979323846f;
	constexpr float64 PI_F64 = 3.14159265358979323846;
	constexpr float32 TWO_PI = 6.28318530717958647692f;
	constexpr float64 TWO_PI_F64 = 6.28318530717958647692;
	constexpr float32 HALF_PI = 1.57079632679489661923f;
	constexpr float64 HALF_PI_F64 = 1.57079632679489661923;

	constexpr float32 DEG_TO_RAD_F32 = PI / 180.0f;
	constexpr float64 DEG_TO_RAD_F64 = PI_F64 / 180.0;
	constexpr float32 RAD_TO_DEG_F32 = 180.0f / PI;
	constexpr float64 RAD_TO_DEG_F64 = 180.0 / PI_F64;

	constexpr float32 EPSILON = 1e-6f;
	constexpr float64 EPSILON_F64 = 1e-12;

	constexpr float32 EULER = 2.71828182845904523536f;
	constexpr float64 EULER_F64 = 2.71828182845904523536;

} // namespace shz