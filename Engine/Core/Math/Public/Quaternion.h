#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Vector4.h"
#include "Engine/Core/Math/Public/Matrix3x3.h"
#include "Engine/Core/Math/Public/Matrix4x4.h"

namespace shz
{
	// ------------------------------------------------------------
	// Quaternion
	// - Storage: (x, y, z, w) where w is the scalar part.
	// - Represents an ACTIVE rotation.
	//
	// Rotation of a vector (active):
	//   v' = q * (0, v) * Conjugate(q)
	//
	// Composition (standard quaternion rule):
	//   qTotal = qB * qA  => applies rotation A, then rotation B.
	//
	// Matrix convention in this project:
	// - Row-major storage
	// - Row-vector convention: v' = v * M
	// - Pre-multiplication friendly
	//
	// Therefore, ToMatrix* outputs matrices suitable for:
	//   v' = v * R
	// ------------------------------------------------------------

	struct Quaternion final
	{
		float32 x, y, z, w; // (vector, scalar)

		// -----------------------------
		// Construction
		// -----------------------------
		constexpr Quaternion() : x(0), y(0), z(0), w(1) {} // Identity
		constexpr Quaternion(float32 _x, float32 _y, float32 _z, float32 _w) : x(_x), y(_y), z(_z), w(_w) {}
		constexpr Quaternion(const Quaternion& other) = default;
		Quaternion& operator=(const Quaternion& other) = default;
		~Quaternion() = default;

		static constexpr Quaternion Identity() { return Quaternion(0, 0, 0, 1); }

		static inline Quaternion FromVector4(const Vector4& v) { return Quaternion(v.x, v.y, v.z, v.w); }
		inline Vector4 ToVector4() const { return Vector4(x, y, z, w); }

		// Axis must be non-zero. Angle in radians.
		static inline Quaternion FromAxisAngle(const Vector3& axis, float32 angleRad)
		{
			Vector3 a = axis;
			const float32 lenSq = a.x * a.x + a.y * a.y + a.z * a.z;
			if (lenSq <= 0.0f)
			{
				return Identity();
			}

			const float32 invLen = 1.0f / std::sqrt(lenSq);
			a.x *= invLen; a.y *= invLen; a.z *= invLen;

			const float32 half = angleRad * 0.5f;
			const float32 s = (float32)std::sin(half);
			const float32 c = (float32)std::cos(half);

			return Quaternion(a.x * s, a.y * s, a.z * s, c);
		}

		inline void GetAxisAngle(Vector3& outAxis, float32& outAngle) const
		{
			const float32 sina2 = (float32)std::sqrt(x * x + y * y + z * z);
			outAngle = 2.0f * (float32)std::atan2(sina2, w);

			const float32 r = (sina2 > 0.0f) ? (1.0f / sina2) : 0.0f;
			outAxis.x = r * x;
			outAxis.y = r * y;
			outAxis.z = r * z;
		}

		// Euler (radians), explicit XYZ intrinsic order:
		// q = qZ * qY * qX  (apply X then Y then Z)
		static inline Quaternion FromEulerXYZ(float32 xRad, float32 yRad, float32 zRad)
		{
			const float32 hx = xRad * 0.5f;
			const float32 hy = yRad * 0.5f;
			const float32 hz = zRad * 0.5f;

			const float32 sx = (float32)std::sin(hx), cx = (float32)std::cos(hx);
			const float32 sy = (float32)std::sin(hy), cy = (float32)std::cos(hy);
			const float32 sz = (float32)std::sin(hz), cz = (float32)std::cos(hz);

			Quaternion qx(sx, 0, 0, cx);
			Quaternion qy(0, sy, 0, cy);
			Quaternion qz(0, 0, sz, cz);

			return qz * qy * qx;
		}

		// -----------------------------
		// Basic queries
		// -----------------------------
		inline float32 LengthSq() const { return x * x + y * y + z * z + w * w; }
		inline float32 Length() const { return (float32)std::sqrt(LengthSq()); }

		inline bool IsFinite() const
		{
			return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
		}

		inline bool IsNormalized(float32 eps = 1e-4f) const
		{
			return (float32)std::fabs(LengthSq() - 1.0f) <= eps;
		}

		static inline float32 Dot(const Quaternion& a, const Quaternion& b)
		{
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		}

		// -----------------------------
		// Conjugate / Inverse / Normalize
		// -----------------------------
		inline Quaternion Conjugate() const { return Quaternion(-x, -y, -z, w); }

		inline Quaternion Inverse() const
		{
			const float32 lsq = LengthSq();
			if (lsq <= 0.0f)
			{
				return Identity();
			}
			const float32 inv = 1.0f / lsq;
			Quaternion c = Conjugate();
			return Quaternion(c.x * inv, c.y * inv, c.z * inv, c.w * inv);
		}

		inline Quaternion Normalized() const
		{
			const float32 lsq = LengthSq();
			if (lsq <= 0.0f)
			{
				return Identity();
			}
			const float32 inv = 1.0f / (float32)std::sqrt(lsq);
			return Quaternion(x * inv, y * inv, z * inv, w * inv);
		}

		inline void Normalize()
		{
			*this = Normalized();
		}

		// Optional: useful to keep continuity in animation/interp.
		inline Quaternion EnsurePositiveW() const
		{
			return (w < 0.0f) ? Quaternion(-x, -y, -z, -w) : *this;
		}

		// -----------------------------
		// Operators
		// -----------------------------
		inline Quaternion operator-() const { return Quaternion(-x, -y, -z, -w); }

		inline Quaternion operator+(const Quaternion& r) const { return Quaternion(x + r.x, y + r.y, z + r.z, w + r.w); }
		inline Quaternion operator-(const Quaternion& r) const { return Quaternion(x - r.x, y - r.y, z - r.z, w - r.w); }

		inline Quaternion operator*(float32 s) const { return Quaternion(x * s, y * s, z * s, w * s); }
		inline Quaternion operator/(float32 s) const
		{
			const float32 inv = 1.0f / s;
			return Quaternion(x * inv, y * inv, z * inv, w * inv);
		}

		// Hamilton product.
		// qTotal = qB * qA => apply A then B.
		inline Quaternion operator*(const Quaternion& r) const
		{
			return Quaternion(
				w * r.x + x * r.w + y * r.z - z * r.y,
				w * r.y - x * r.z + y * r.w + z * r.x,
				w * r.z + x * r.y - y * r.x + z * r.w,
				w * r.w - x * r.x - y * r.y - z * r.z
			);
		}

		inline Quaternion& operator*=(const Quaternion& r)
		{
			*this = (*this) * r;
			return *this;
		}

		// -----------------------------
		// Rotate vector (active rotation)
		// -----------------------------
		inline Vector3 RotateVector(const Vector3& v) const
		{
			// Optimized form of: q * (0,v) * conj(q)
			// v' = v + 2*w*(qv x v) + 2*(qv x (qv x v))
			const Vector3 qv(x, y, z);
			const Vector3 t = (qv.Cross(v)) * 2.0f;
			return v + t * w + qv.Cross(t);
		}

		// -----------------------------
		// Interpolation
		// -----------------------------
		static inline Quaternion Nlerp(const Quaternion& a, const Quaternion& b, float32 t)
		{
			Quaternion bb = b;
			if (Dot(a, b) < 0.0f)
			{
				bb = -b;
			}

			Quaternion r = a * (1.0f - t) + bb * t;
			return r.Normalized();
		}

		static inline Quaternion Slerp(const Quaternion& a, const Quaternion& b, float32 t)
		{
			Quaternion bb = b;
			float32 cosTheta = Dot(a, b);

			if (cosTheta < 0.0f)
			{
				bb = -b;
				cosTheta = -cosTheta;
			}

			const float32 kThreshold = 0.9995f;
			if (cosTheta > kThreshold)
			{
				return Nlerp(a, bb, t);
			}

			const float32 theta = (float32)std::acos(cosTheta);
			const float32 sinTheta = (float32)std::sin(theta);
			const float32 wA = (float32)std::sin((1.0f - t) * theta) / sinTheta;
			const float32 wB = (float32)std::sin(t * theta) / sinTheta;

			Quaternion r = a * wA + bb * wB;
			return r.Normalized();
		}

		// -----------------------------
		// Matrix conversion (Row-vector)
		// -----------------------------
		inline Matrix3x3 ToMatrix3x3() const
		{
			// Build standard (column-vector) rotation matrix from quaternion, then transpose.
			// Column-vector matrix (active rotation):
			// [ 1-2y^2-2z^2   2xy-2zw     2xz+2yw ]
			// [ 2xy+2zw       1-2x^2-2z^2 2yz-2xw ]
			// [ 2xz-2yw       2yz+2xw     1-2x^2-2y^2 ]
			const float32 xx = x * x;
			const float32 yy = y * y;
			const float32 zz = z * z;
			const float32 xy = x * y;
			const float32 xz = x * z;
			const float32 yz = y * z;
			const float32 xw = x * w;
			const float32 yw = y * w;
			const float32 zw = z * w;

			const Matrix3x3 col(
				1.0f - 2.0f * (yy + zz), 2.0f * (xy - zw), 2.0f * (xz + yw),
				2.0f * (xy + zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - xw),
				2.0f * (xz - yw), 2.0f * (yz + xw), 1.0f - 2.0f * (xx + yy)
			);

			return col.Transposed();
		}

		inline Matrix4x4 ToMatrix4x4() const
		{
			const Matrix3x3 R = ToMatrix3x3();
			return Matrix4x4(
				R._m00, R._m01, R._m02, 0.0f,
				R._m10, R._m11, R._m12, 0.0f,
				R._m20, R._m21, R._m22, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f
			);
		}
	};
	static_assert(sizeof(Quaternion) == 4 * sizeof(float32), "Quaternion size is incorrect.");
	static_assert(alignof(Quaternion) == alignof(float32), "Quaternion alignment is incorrect.");

} // namespace shz