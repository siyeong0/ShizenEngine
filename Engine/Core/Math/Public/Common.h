#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Public/Constants.h"
#include <cmath>

namespace shz
{
	inline float32 Sin(float32 angleRad)
	{
		return std::sin(angleRad);
	}

	inline float32 Cos(float32 angleRad)
	{
		return std::cos(angleRad);
	}

	inline float32 Tan(float32 angleRad)
	{
		return std::tan(angleRad);
	}

	inline float32 Asin(float32 value)
	{
		return std::asin(value);
	}

	inline float32 Acos(float32 value)
	{
		return std::acos(value);
	}

	inline float32 Atan(float32 value)
	{
		return std::atan(value);
	}

	inline float32 Atan2(float32 y, float32 x)
	{
		return std::atan2(y, x);
	}

	inline float32 Sqrt(float32 value)
	{
		return std::sqrt(value);
	}

	inline float32 Abs(float32 value)
	{
		return std::fabs(value);
	}

	inline float32 Pow(float32 base, float32 exponent)
	{
		return std::pow(base, exponent);
	}

	inline float32 Exp(float32 exponent)
	{
		return std::exp(exponent);
	}

	inline float32 Log(float32 value)
	{
		return std::log(value);
	}

	inline float32 Log10(float32 value)
	{
		return std::log10(value);
	}

	inline float32 Min(float32 a, float32 b)
	{
		return (a < b) ? a : b;
	}

	inline float32 Max(float32 a, float32 b)
	{
		return (a > b) ? a : b;
	}

	inline float32 Mod(float32 a, float32 b)
	{
		return std::fmod(a, b);
	}

	inline float32 Clamp(float32 value, float32 min, float32 max)
	{
		if (value < min) return min;
		if (value > max) return max;
		return value;
	}

	inline float32 Clamp01(float32 value)
	{
		return Clamp(value, 0.0f, 1.0f);
	}

	inline float32 Lerp(float32 a, float32 b, float32 t)
	{
		return a + t * (b - a);
	}

	inline bool IsNearlyEqual(float32 a, float32 b, float32 epsilon = 1e-6f)
	{
		return Abs(a - b) <= epsilon;
	}

	inline bool IsNearlyZero(float32 value, float32 epsilon = 1e-6f)
	{
		return Abs(value) <= epsilon;
	}

	inline int RoundToInt(float32 value)
	{
		return static_cast<int>(std::round(value));
	}

	inline float32 Floor(float32 value)
	{
		return std::floor(value);
	}

	inline float32 Ceil(float32 value)
	{
		return std::ceil(value);
	}

	inline float32 Frac(float32 value)
	{
		return value - Floor(value);
	}

	inline float32 Sign(float32 value)
	{
		return (value > 0.f) ? 1.f : ((value < 0.f) ? -1.f : 0.f);
	}

	inline float32 SmoothStep(float32 edge0, float32 edge1, float32 x)
	{
		// Scale, bias and saturate x to 0..1 range
		x = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		// Evaluate polynomial
		return x * x * (3 - 2 * x);
	}

	inline float32 DegToRad(float32 degrees)
	{
		return degrees * (PI / 180.0f);
	}

	inline float32 RadToDeg(float32 radians)
	{
		return radians * (180.0f / PI);
	}

} // namespace shz