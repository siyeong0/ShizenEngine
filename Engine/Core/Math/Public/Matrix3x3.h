#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#include "Vector3.h"

namespace shz
{
	// ------------------------------------------------------------
	// Matrix3x3
	// - Row-major storage
	// - Row-vector convention (v' = v * M)
	// - Pre-multiplication friendly: v * (A * B) == (v * A) * B
	// ------------------------------------------------------------
	struct Matrix3x3 final
	{
		union
		{
			float32 m[3][3];

			struct
			{
				// Row-major named elements.
				float32 _m00, _m01, _m02;
				float32 _m10, _m11, _m12;
				float32 _m20, _m21, _m22;
			};
		};

		// --------------------------------------------------------
		// Constructors
		// --------------------------------------------------------
		constexpr Matrix3x3()
			: _m00(1), _m01(0), _m02(0)
			, _m10(0), _m11(1), _m12(0)
			, _m20(0), _m21(0), _m22(1)
		{
		}

		constexpr Matrix3x3(
			float32 m00, float32 m01, float32 m02,
			float32 m10, float32 m11, float32 m12,
			float32 m20, float32 m21, float32 m22)
			: _m00(m00), _m01(m01), _m02(m02)
			, _m10(m10), _m11(m11), _m12(m12)
			, _m20(m20), _m21(m21), _m22(m22)
		{
		}

		constexpr Matrix3x3(const Matrix3x3& other) = default;
		Matrix3x3& operator=(const Matrix3x3& other) = default;
		~Matrix3x3() = default;


		// --------------------------------------------------------
		// Factory
		// --------------------------------------------------------
		static inline Matrix3x3 Identity() { return Matrix3x3(); }

		static inline Matrix3x3 Zero()
		{
			return Matrix3x3(
				0, 0, 0,
				0, 0, 0,
				0, 0, 0);
		}

		static inline Matrix3x3 FromRows(const Vector3& r0, const Vector3& r1, const Vector3& r2)
		{
			return Matrix3x3(
				r0.x, r0.y, r0.z,
				r1.x, r1.y, r1.z,
				r2.x, r2.y, r2.z);
		}

		static inline Matrix3x3 FromCols(const Vector3& c0, const Vector3& c1, const Vector3& c2)
		{
			// c0,c1,c2 are columns => write them into row-major storage.
			return Matrix3x3(
				c0.x, c1.x, c2.x,
				c0.y, c1.y, c2.y,
				c0.z, c1.z, c2.z);
		}

		// --------------------------------------------------------
		// Transform
		// --------------------------------------------------------
		// Row-vector: v' = v * M
		inline Vector3 MulVector(const Vector3& v) const
		{
			return Vector3(
				v.x * _m00 + v.y * _m10 + v.z * _m20,
				v.x * _m01 + v.y * _m11 + v.z * _m21,
				v.x * _m02 + v.y * _m12 + v.z * _m22);
		}

		// --------------------------------------------------------
		// Algebra
		// --------------------------------------------------------
		inline Matrix3x3 Transposed() const
		{
			return Matrix3x3(
				_m00, _m10, _m20,
				_m01, _m11, _m21,
				_m02, _m12, _m22);
		}

		inline float32 Determinant() const
		{
			// |a b c|
			// |d e f| = a(ei - fh) - b(di - fg) + c(dh - eg)
			// |g h i|
			const float32 a = _m00, b = _m01, c = _m02;
			const float32 d = _m10, e = _m11, f = _m12;
			const float32 g = _m20, h = _m21, i = _m22;
			return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
		}

		inline Matrix3x3 Inversed() const
		{
			// General 3x3 inverse via adjugate.
			const float32 det = Determinant();
			ASSERT(std::fabs(det) > 1e-12f, "Attempted to invert a matrix with zero determinant.");
			const float32 invDet = 1.0f / det;

			const float32 a = _m00, b = _m01, c = _m02;
			const float32 d = _m10, e = _m11, f = _m12;
			const float32 g = _m20, h = _m21, i = _m22;

			return Matrix3x3(
				+(e * i - f * h) * invDet,
				-(b * i - c * h) * invDet,
				+(b * f - c * e) * invDet,

				-(d * i - f * g) * invDet,
				+(a * i - c * g) * invDet,
				-(a * f - c * d) * invDet,

				+(d * h - e * g) * invDet,
				-(a * h - b * g) * invDet,
				+(a * e - b * d) * invDet);
		}

		inline Matrix3x3 operator*(const Matrix3x3& rhs) const
		{
			Matrix3x3 r = Zero();
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					r.m[i][j] =
						m[i][0] * rhs.m[0][j] +
						m[i][1] * rhs.m[1][j] +
						m[i][2] * rhs.m[2][j];
				}
			}
			return r;
		}

		// --------------------------------------------------------
		// Common engine helpers (LH)
		// Note: These are written for row-vector convention.
		// --------------------------------------------------------
		static inline Matrix3x3 RotationX(float32 rad)
		{
			const float32 c = (float32)std::cos(rad);
			const float32 s = (float32)std::sin(rad);
			return Matrix3x3(
				1, 0, 0,
				0, c, s,
				0, -s, c);
		}

		static inline Matrix3x3 RotationY(float32 rad)
		{
			const float32 c = (float32)std::cos(rad);
			const float32 s = (float32)std::sin(rad);
			return Matrix3x3(
				c, 0, -s,
				0, 1, 0,
				s, 0, c);
		}

		static inline Matrix3x3 RotationZ(float32 rad)
		{
			const float32 c = (float32)std::cos(rad);
			const float32 s = (float32)std::sin(rad);
			return Matrix3x3(
				c, s, 0,
				-s, c, 0,
				0, 0, 1);
		}

		static inline Matrix3x3 RotationAxis(Vector3 axis, float32 rad)
		{
			axis = axis.Normalized();

			const float32 x = axis.x;
			const float32 y = axis.y;
			const float32 z = axis.z;

			const float32 c = (float32)std::cos(rad);
			const float32 s = -(float32)std::sin(rad); // row-vector fix (same as Matrix4x4::RotationArbitrary)
			const float32 t = 1.0f - c;

			// Row-vector convention matrix (v' = v * R)
			return Matrix3x3(
				t * x * x + c, t * x * y + s * z, t * x * z - s * y,
				t * y * x - s * z, t * y * y + c, t * y * z + s * x,
				t * z * x + s * y, t * z * y - s * x, t * z * z + c
			);
		}

	};
	static_assert(sizeof(Matrix3x3) == sizeof(float32) * 9, "Matrix3x3 size is incorrect.");
	static_assert(alignof(Matrix3x3) == alignof(float32), "Matrix3x3 alignment is incorrect.");

} // namespace shz