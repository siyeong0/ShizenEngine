#include "pch.h"
#include "Engine/Core/Math/Public/Vector4.h"

#include "Engine/Core/Math/Public/Vector2.h"
#include "Engine/Core/Math/Public/Vector3.h"

namespace shz
{
	Vector4::Vector4(const Vector2& v, float z, float w)
		: x(v.x)
		, y(v.y)
		, z(z)
		, w(w)
	{

	}

	Vector4::Vector4(const Vector2& v1, const Vector2& v2)
		: x(v1.x)
		, y(v1.y)
		, z(v2.x)
		, w(v2.y)
	{

	}

	Vector4::Vector4(const Vector3& v, float w)
		: x(v.x)
		, y(v.y)
		, z(v.z)
		, w(w)
	{

	}

	Vector4::operator Vector3() const
	{
		return Vector3{ x, y, z };
	}

	Vector3 Vector4::ToVector3() const
	{
		return Vector3{ x, y, z };
	}

} // namespace shz