#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#include "Vector2.h"

namespace shz
{
	// ------------------------------------------------------------
	// Matrix2x2
	// - Row-major storage
	// - Row-vector convention (v' = v * M)
	// - Pre-multiplication friendly: v * (A * B) == (v * A) * B
	// ------------------------------------------------------------
	struct Matrix2x2 final
	{
		union
		{
			float32 m[2][2];

			struct
			{
				// Row-major named elements.
				float32 _m00, _m01;
				float32 _m10, _m11;
			};
		};

		// --------------------------------------------------------
		// Constructors
		// --------------------------------------------------------
		constexpr Matrix2x2()
			: _m00(1), _m01(0)
			, _m10(0), _m11(1)
		{
		}

		constexpr Matrix2x2(
			float32 m00, float32 m01,
			float32 m10, float32 m11)
			: _m00(m00), _m01(m01)
			, _m10(m10), _m11(m11)
		{
		}

		constexpr Matrix2x2(const Matrix2x2& other) = default;
		Matrix2x2& operator=(const Matrix2x2& other) = default;
		~Matrix2x2() = default;

		// --------------------------------------------------------
		// Factory
		// --------------------------------------------------------
		static inline Matrix2x2 Identity() { return Matrix2x2(); }

		static inline Matrix2x2 Zero()
		{
			return Matrix2x2(
				0, 0,
				0, 0);
		}

		static inline Matrix2x2 FromRows(const Vector2& r0, const Vector2& r1)
		{
			return Matrix2x2(
				r0.x, r0.y,
				r1.x, r1.y);
		}

		static inline Matrix2x2 FromCols(const Vector2& c0, const Vector2& c1)
		{
			// c0,c1 are columns => write them into row-major storage.
			return Matrix2x2(
				c0.x, c1.x,
				c0.y, c1.y);
		}

		// --------------------------------------------------------
		// Transform
		// --------------------------------------------------------
		// Row-vector: v' = v * M
		inline Vector2 MulVector(const Vector2& v) const
		{
			return Vector2(
				v.x * _m00 + v.y * _m10,
				v.x * _m01 + v.y * _m11);
		}

		// --------------------------------------------------------
		// Algebra
		// --------------------------------------------------------
		inline Matrix2x2 Transposed() const
		{
			return Matrix2x2(
				_m00, _m10,
				_m01, _m11);
		}

		inline float32 Determinant() const
		{
			// |a b|
			// |c d| => ad - bc
			return _m00 * _m11 - _m01 * _m10;
		}

		inline Matrix2x2 Inversed() const
		{
			// Inverse:
			// 1/det * | d -b|
			//         |-c  a|
			const float32 det = Determinant();
			ASSERT(std::fabs(det) > 1e-12f, "Attempted to invert a matrix with zero determinant.");
			const float32 invDet = 1.0f / det;

			return Matrix2x2(
				+_m11 * invDet, -_m01 * invDet,
				-_m10 * invDet, +_m00 * invDet);
		}

		inline Matrix2x2 operator*(const Matrix2x2& rhs) const
		{
			// Row-major matrix multiply: r = this * rhs
			return Matrix2x2(
				_m00 * rhs._m00 + _m01 * rhs._m10,
				_m00 * rhs._m01 + _m01 * rhs._m11,

				_m10 * rhs._m00 + _m11 * rhs._m10,
				_m10 * rhs._m01 + _m11 * rhs._m11);
		}

		// --------------------------------------------------------
		// Common engine helpers (LH)
		// Note: These are written for row-vector convention.
		// --------------------------------------------------------
		static inline Matrix2x2 Rotation(float32 rad)
		{
			const float32 c = (float32)std::cos(rad);
			const float32 s = (float32)std::sin(rad);

			// For row-vector convention, use the transpose of the
			// usual column-vector rotation matrix.
			//
			// Column-vector: | c -s |
			//                | s  c |
			// Row-vector:    | c  s |
			//                |-s  c |
			return Matrix2x2(
				c, s,
				-s, c);
		}

		static inline Matrix2x2 Scale(float32 sx, float32 sy)
		{
			// Row-vector scaling: (x',y') = (x,y) * S
			// => x' = x*sx, y' = y*sy
			return Matrix2x2(
				sx, 0,
				0, sy);
		}

		static inline Matrix2x2 Scale(const Vector2& s)
		{
			return Scale(s.x, s.y);
		}
	};

	static_assert(sizeof(Matrix2x2) == sizeof(float32) * 4, "Matrix2x2 size is incorrect.");
	static_assert(alignof(Matrix2x2) == alignof(float32), "Matrix2x2 alignment is incorrect.");

} // namespace shz
