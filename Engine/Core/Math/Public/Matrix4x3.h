#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Matrix3x3.h"
#include "Engine/Core/Math/Public/Matrix4x4.h"

namespace shz
{
	// ------------------------------------------------------------
	// Matrix4x3
	// - Row-major storage (4 rows x 3 columns)
	// - Affine transform for row-vector convention (p' = p * M)
	//   where p is treated as (x,y,z,1).
	// - Equivalent 4x4 form is:
	//     [ R  0 ]
	//     [ t  1 ]
	//   with R (3x3) in the first 3 rows, and translation t in the 4th row.
	// ------------------------------------------------------------
	struct Matrix4x3 final
	{
		union
		{
			float32 m[4][3];
			struct
			{
				float32 _m00, _m01, _m02;
				float32 _m10, _m11, _m12;
				float32 _m20, _m21, _m22;
				float32 _m30, _m31, _m32;
			};
		};

		// --------------------------------------------------------
		// Constructors
		// --------------------------------------------------------
		constexpr Matrix4x3()
			: _m00(1), _m01(0), _m02(0)
			, _m10(0), _m11(1), _m12(0)
			, _m20(0), _m21(0), _m22(1)
			, _m30(0), _m31(0), _m32(0)
		{
		}

		constexpr Matrix4x3(
			float32 m00, float32 m01, float32 m02,
			float32 m10, float32 m11, float32 m12,
			float32 m20, float32 m21, float32 m22,
			float32 m30, float32 m31, float32 m32)
			: _m00(m00), _m01(m01), _m02(m02)
			, _m10(m10), _m11(m11), _m12(m12)
			, _m20(m20), _m21(m21), _m22(m22)
			, _m30(m30), _m31(m31), _m32(m32)
		{
		}

		constexpr Matrix4x3(const Matrix4x3& other) = default;
		Matrix4x3& operator=(const Matrix4x3& other) = default;
		~Matrix4x3() = default;

		// --------------------------------------------------------
		// Factory
		// --------------------------------------------------------
		static inline Matrix4x3 Identity() { return Matrix4x3(); }

		static inline Matrix4x3 Zero()
		{
			return Matrix4x3(
				0, 0, 0,
				0, 0, 0,
				0, 0, 0,
				0, 0, 0);
		}

		static inline Matrix4x3 FromRotationTranslation(const Matrix3x3& R, const Vector3& t)
		{
			Matrix4x3 r{};
			r._m00 = R._m00; r._m01 = R._m01; r._m02 = R._m02;
			r._m10 = R._m10; r._m11 = R._m11; r._m12 = R._m12;
			r._m20 = R._m20; r._m21 = R._m21; r._m22 = R._m22;
			r._m30 = t.x;    r._m31 = t.y;    r._m32 = t.z;
			return r;
		}

		static inline Matrix4x3 Translation(const Vector3& t)
		{
			return FromRotationTranslation(Matrix3x3::Identity(), t);
		}

		static inline Matrix4x3 Scale(const Vector3& s)
		{
			const Matrix3x3 R(
				s.x, 0, 0,
				0, s.y, 0,
				0, 0, s.z);
			return FromRotationTranslation(R, Vector3(0, 0, 0));
		}

		static inline Matrix4x3 TRS(const Vector3& translation, const Vector3& rotationEuler, const Vector3& scale)
		{
			// Euler is assumed in radians.
			// With row vectors: v' = v * (S * R * T)
			const Matrix3x3 Rx = Matrix3x3::RotationX(rotationEuler.x);
			const Matrix3x3 Ry = Matrix3x3::RotationY(rotationEuler.y);
			const Matrix3x3 Rz = Matrix3x3::RotationZ(rotationEuler.z);
			const Matrix3x3 R = (Rx * Ry) * Rz;
			const Matrix3x3 S(
				scale.x, 0, 0,
				0, scale.y, 0,
				0, 0, scale.z);
			const Matrix3x3 RS = S * R; // apply scale then rotation
			return FromRotationTranslation(RS, translation);
		}

		static inline Matrix4x3 FromMatrix4x4(const Matrix4x4& M)
		{
			// Assumes affine (last column == [0,0,0,1]^T)
			ASSERT(std::fabs(M._m03) < 1e-6f && std::fabs(M._m13) < 1e-6f && std::fabs(M._m23) < 1e-6f);
			ASSERT(std::fabs(M._m33 - 1.0f) < 1e-6f);

			Matrix4x3 r{};
			r._m00 = M._m00; r._m01 = M._m01; r._m02 = M._m02;
			r._m10 = M._m10; r._m11 = M._m11; r._m12 = M._m12;
			r._m20 = M._m20; r._m21 = M._m21; r._m22 = M._m22;
			r._m30 = M._m30; r._m31 = M._m31; r._m32 = M._m32;
			return r;
		}

		// --------------------------------------------------------
		// Extract / Convert
		// --------------------------------------------------------
		inline Vector3 ExtractTranslation() const { return Vector3(_m30, _m31, _m32); }

		inline Matrix3x3 ExtractLinearMatrix() const
		{
			return Matrix3x3(
				_m00, _m01, _m02,
				_m10, _m11, _m12,
				_m20, _m21, _m22);
		}

		inline Matrix4x4 ToMatrix4x4() const
		{
			return Matrix4x4(
				_m00, _m01, _m02, 0,
				_m10, _m11, _m12, 0,
				_m20, _m21, _m22, 0,
				_m30, _m31, _m32, 1);
		}

		// --------------------------------------------------------
		// Transform
		// --------------------------------------------------------
		inline Vector3 TransformPosition(const Vector3& p) const
		{
			return Vector3(
				p.x * _m00 + p.y * _m10 + p.z * _m20 + _m30,
				p.x * _m01 + p.y * _m11 + p.z * _m21 + _m31,
				p.x * _m02 + p.y * _m12 + p.z * _m22 + _m32);
		}

		inline Vector3 TransformDirection(const Vector3& d) const
		{
			return Vector3(
				d.x * _m00 + d.y * _m10 + d.z * _m20,
				d.x * _m01 + d.y * _m11 + d.z * _m21,
				d.x * _m02 + d.y * _m12 + d.z * _m22);
		}

		// --------------------------------------------------------
		// Composition
		// --------------------------------------------------------
		inline Matrix4x3 operator*(const Matrix4x3& rhs) const
		{
			// [R1 0; t1 1] * [R2 0; t2 1] = [R1*R2 0; t1*R2 + t2 1]
			Matrix4x3 r = Zero();

			// R = R1 * R2
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

			// t = t1 * R2 + t2
			r._m30 = _m30 * rhs._m00 + _m31 * rhs._m10 + _m32 * rhs._m20 + rhs._m30;
			r._m31 = _m30 * rhs._m01 + _m31 * rhs._m11 + _m32 * rhs._m21 + rhs._m31;
			r._m32 = _m30 * rhs._m02 + _m31 * rhs._m12 + _m32 * rhs._m22 + rhs._m32;
			return r;
		}

		// --------------------------------------------------------
		// Inverse
		// --------------------------------------------------------
		inline Matrix4x3 InverseAffine() const
		{
			const Matrix3x3 R = ExtractLinearMatrix();
			const Matrix3x3 invR = R.Inversed();
			const Vector3 t = ExtractTranslation();
			const Vector3 invT = invR.MulVector(Vector3(-t.x, -t.y, -t.z));
			return FromRotationTranslation(invR, invT);
		}
	};
	static_assert(sizeof(Matrix4x3) == sizeof(float32) * 12, "Matrix4x3 size is incorrect.");
	static_assert(alignof(Matrix4x3) == alignof(float32), "Matrix4x3 alignment is incorrect.");

}