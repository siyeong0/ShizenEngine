#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Vector4.h"
#include "Engine/Core/Math/Public/Matrix3x3.h"

namespace shz
{
	// ------------------------------------------------------------
	// Matrix4x4
	// - Row-major storage
	// - Row vector convention (v' = v * M)
	// - Pre-multiplication friendly: v * (A * B) == (v * A) * B
	// - Translation lives in the last row (m30,m31,m32)
	// ------------------------------------------------------------
	struct Matrix4x4 final
	{
		union
		{
			float32 m[4][4];

			struct
			{
				float32 _m00, _m01, _m02, _m03;
				float32 _m10, _m11, _m12, _m13;
				float32 _m20, _m21, _m22, _m23;
				float32 _m30, _m31, _m32, _m33;
			};
		};

		// --------------------------------------------------------
		// Constructors
		// --------------------------------------------------------
		constexpr Matrix4x4()
			: _m00(1), _m01(0), _m02(0), _m03(0)
			, _m10(0), _m11(1), _m12(0), _m13(0)
			, _m20(0), _m21(0), _m22(1), _m23(0)
			, _m30(0), _m31(0), _m32(0), _m33(1)
		{
		}

		constexpr Matrix4x4(
			float32 m00, float32 m01, float32 m02, float32 m03,
			float32 m10, float32 m11, float32 m12, float32 m13,
			float32 m20, float32 m21, float32 m22, float32 m23,
			float32 m30, float32 m31, float32 m32, float32 m33)
			: _m00(m00), _m01(m01), _m02(m02), _m03(m03)
			, _m10(m10), _m11(m11), _m12(m12), _m13(m13)
			, _m20(m20), _m21(m21), _m22(m22), _m23(m23)
			, _m30(m30), _m31(m31), _m32(m32), _m33(m33)
		{
		}

		constexpr Matrix4x4(const Matrix4x4& other) = default;
		Matrix4x4& operator=(const Matrix4x4& other) = default;
		~Matrix4x4() = default;

		// --------------------------------------------------------
		// Conversion
		// s--------------------------------------------------------
		explicit constexpr operator Matrix3x3() const noexcept
		{
			return Matrix3x3(
				_m00, _m01, _m02,
				_m10, _m11, _m12,
				_m20, _m21, _m22);
		}

		// --------------------------------------------------------
		// Factory
		// --------------------------------------------------------
		static inline Matrix4x4 Identity() { return Matrix4x4(); }

		static inline Matrix4x4 Zero()
		{
			return Matrix4x4(
				0, 0, 0, 0,
				0, 0, 0, 0,
				0, 0, 0, 0,
				0, 0, 0, 0);
		}

		static inline Matrix4x4 Translation(const Vector3& t)
		{
			return Matrix4x4(
				1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				t.x, t.y, t.z, 1);
		}

		static inline Matrix4x4 Scale(const Vector3& s)
		{
			return Matrix4x4(
				s.x, 0, 0, 0,
				0, s.y, 0, 0,
				0, 0, s.z, 0,
				0, 0, 0, 1);
		}

		static inline Matrix4x4 RotationX(float32 rad)
		{
			const float32 c = (float32)std::cos(rad);
			const float32 s = (float32)std::sin(rad);
			return Matrix4x4(
				1, 0, 0, 0,
				0, c, s, 0,
				0, -s, c, 0,
				0, 0, 0, 1);
		}

		static inline Matrix4x4 RotationY(float32 rad)
		{
			const float32 c = (float32)std::cos(rad);
			const float32 s = (float32)std::sin(rad);
			return Matrix4x4(
				c, 0, -s, 0,
				0, 1, 0, 0,
				s, 0, c, 0,
				0, 0, 0, 1);
		}

		static inline Matrix4x4 RotationZ(float32 rad)
		{
			const float32 c = (float32)std::cos(rad);
			const float32 s = (float32)std::sin(rad);
			return Matrix4x4(
				c, s, 0, 0,
				-s, c, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1);
		}

		static inline Matrix4x4 RotationAxis(const Vector3& axis, float32 rad)
		{
			const Matrix3x3 R = Matrix3x3::RotationAxis(axis, rad);
			return Matrix4x4(
				R._m00, R._m01, R._m02, 0,
				R._m10, R._m11, R._m12, 0,
				R._m20, R._m21, R._m22, 0,
				0, 0, 0, 1);
		}

		// 3D Rotation matrix for an arbitrary axis specified by x, y and z
		static inline Matrix4x4 RotationArbitrary(Vector3 axis, float32 angleInRadians)
		{
			axis = Vector3::Normalize(axis);

			float32 sinAngle = std::sin(angleInRadians);
			float32 cosAngle = std::cos(angleInRadians);
			float32 oneMinusCosAngle = 1 - cosAngle;

			Matrix4x4 mOut;

			mOut._m00 = 1 + oneMinusCosAngle * (axis.x * axis.x - 1);
			mOut._m01 = axis.z * sinAngle + oneMinusCosAngle * axis.x * axis.y;
			mOut._m02 = -axis.y * sinAngle + oneMinusCosAngle * axis.x * axis.z;
			mOut._m30 = 0;

			mOut._m10 = -axis.z * sinAngle + oneMinusCosAngle * axis.y * axis.x;
			mOut._m11 = 1 + oneMinusCosAngle * (axis.y * axis.y - 1);
			mOut._m12 = axis.x * sinAngle + oneMinusCosAngle * axis.y * axis.z;
			mOut._m13 = 0;

			mOut._m20 = axis.y * sinAngle + oneMinusCosAngle * axis.z * axis.x;
			mOut._m21 = -axis.x * sinAngle + oneMinusCosAngle * axis.z * axis.y;
			mOut._m22 = 1 + oneMinusCosAngle * (axis.z * axis.z - 1);
			mOut._m23 = 0;

			mOut._m30 = 0;
			mOut._m31 = 0;
			mOut._m32 = 0;
			mOut._m33 = 1;

			return mOut;
		}

		static inline Matrix4x4 TRS(const Vector3& translation, const Vector3& rotationEuler, const Vector3& scale)
		{
			// Euler is assumed in radians.
			// With row vectors: v' = v * (S * R * T) applies S then R then T.
			const Matrix4x4 Rx = RotationX(rotationEuler.x);
			const Matrix4x4 Ry = RotationY(rotationEuler.y);
			const Matrix4x4 Rz = RotationZ(rotationEuler.z);
			const Matrix4x4 R = (Rx * Ry) * Rz;
			const Matrix4x4 S = Scale(scale);
			const Matrix4x4 T = Translation(translation);
			return (S * R) * T;
		}

		// --------------------------------------------------------
		// Camera (row-vector, LH)
		// --------------------------------------------------------
		static inline Matrix4x4 LookAtLH(const Vector3& eye, const Vector3& at, const Vector3& up)
		{
			const Vector3 zaxis = (at - eye).Normalized();
			const Vector3 xaxis = up.Cross(zaxis).Normalized();
			const Vector3 yaxis = zaxis.Cross(xaxis);

			return Matrix4x4(
				xaxis.x, xaxis.y, xaxis.z, 0,
				yaxis.x, yaxis.y, yaxis.z, 0,
				zaxis.x, zaxis.y, zaxis.z, 0,
				-xaxis.Dot(eye), -yaxis.Dot(eye), -zaxis.Dot(eye), 1);
		}

		static inline Matrix4x4 PerspectiveFovLH(float32 fovY, float32 aspect, float32 zn, float32 zf)
		{
			// DirectX LH perspective (depth 0..1) for row-vector convention.
			ASSERT(fovY > 0.0f && aspect > 0.0f);
			ASSERT(zf > zn);

			const float32 yScale = 1.0f / (float32)std::tan(fovY * 0.5f);
			const float32 xScale = yScale / aspect;
			const float32 A = zf / (zf - zn);
			const float32 B = (-zn * zf) / (zf - zn);

			// row-vector LH projection (D3D depth 0..1)
			return Matrix4x4(
				xScale, 0, 0, 0,
				0, yScale, 0, 0,
				0, 0, A, 1,
				0, 0, B, 0);
		}

		void SetNearFarClipPlanes(float32 zNear, float32 zFar, bool NegativeOneToOneZ)
		{
			if (_m33 == 0)
			{
				// Perspective projection
				if (NegativeOneToOneZ)
				{
					// https://www.opengl.org/sdk/docs/man2/xhtml/gluPerspective.xml
					// http://www.terathon.com/gdc07_lengyel.pdf
					// Note that OpenGL uses right-handed coordinate system, where
					// camera is looking in negative z direction:
					//   OO
					//  |__|<--------------------
					//         -z             +z
					// Consequently, OpenGL projection matrix given by these two
					// references inverts z axis.

					// We do not need to do this, because we use DX coordinate
					// system for the camera space. Thus we need to invert the
					// sign of the values in the third column in the matrix
					// from the references:

					_m22 = -(-(zFar + zNear) / (zFar - zNear));
					_m32 = -2 * zNear * zFar / (zFar - zNear);
					_m23 = -(-1);
				}
				else
				{
					_m22 = zFar / (zFar - zNear);
					_m32 = -zNear * zFar / (zFar - zNear);
					_m23 = 1;
				}
			}
			else
			{
				// Orthographic projection
				_m22 = (NegativeOneToOneZ ? 2 : 1) / (zFar - zNear);
				_m32 = (NegativeOneToOneZ ? zNear + zFar : zNear) / (zNear - zFar);
			}
		}

		// --------------------------------------------------------
		// Transform (row-vector): v' = v * M
		// --------------------------------------------------------
		inline Vector4 MulVector4(const Vector4& v) const
		{
			return Vector4(
				v.x * _m00 + v.y * _m10 + v.z * _m20 + v.w * _m30,
				v.x * _m01 + v.y * _m11 + v.z * _m21 + v.w * _m31,
				v.x * _m02 + v.y * _m12 + v.z * _m22 + v.w * _m32,
				v.x * _m03 + v.y * _m13 + v.z * _m23 + v.w * _m33);
		}

		inline Vector3 TransformPosition(const Vector3& p) const
		{
			const Vector4 r = MulVector4(Vector4(p.x, p.y, p.z, 1.0f));
			ASSERT(std::fabs(r.w) > 1e-12f);
			return Vector3(r.x / r.w, r.y / r.w, r.z / r.w);
		}

		inline Vector3 TransformDirection(const Vector3& d) const
		{
			const Vector4 r = MulVector4(Vector4(d.x, d.y, d.z, 0.0f));
			return Vector3(r.x, r.y, r.z);
		}

		// --------------------------------------------------------
		// Extract (UE/DX style)
		// --------------------------------------------------------
		inline Vector3 ExtractTranslation() const { return Vector3(_m30, _m31, _m32); }
		inline Vector3 ExtractAxisX() const { return Vector3(_m00, _m01, _m02); } // row0
		inline Vector3 ExtractAxisY() const { return Vector3(_m10, _m11, _m12); } // row1
		inline Vector3 ExtractAxisZ() const { return Vector3(_m20, _m21, _m22); } // row2

		// --------------------------------------------------------
		// Algebra
		// --------------------------------------------------------
		inline Matrix4x4 Transposed() const
		{
			Matrix4x4 r{};
			for (int i = 0; i < 4; ++i)
			{
				for (int j = 0; j < 4; ++j)
				{
					r.m[i][j] = m[j][i];
				}
			}
			return r;
		}

		inline Matrix4x4 operator*(const Matrix4x4& rhs) const
		{
			Matrix4x4 r = Zero();
			for (int i = 0; i < 4; ++i)
			{
				for (int j = 0; j < 4; ++j)
				{
					r.m[i][j] =
						m[i][0] * rhs.m[0][j] +
						m[i][1] * rhs.m[1][j] +
						m[i][2] * rhs.m[2][j] +
						m[i][3] * rhs.m[3][j];
				}
			}
			return r;
		}

		inline Matrix4x4 Inversed() const
		{
			// General 4x4 inverse (Gauss-Jordan). Stable enough for engine use.
			Matrix4x4 a = *this;
			Matrix4x4 inv = Identity();

			for (int col = 0; col < 4; ++col)
			{
				// Find pivot.
				int pivotRow = col;
				float32 pivotAbs = (float32)std::fabs(a.m[pivotRow][col]);
				for (int r = col + 1; r < 4; ++r)
				{
					const float32 v = (float32)std::fabs(a.m[r][col]);
					if (v > pivotAbs)
					{
						pivotAbs = v;
						pivotRow = r;
					}
				}

				ASSERT(pivotAbs > 1e-12f);

				// Swap rows if needed.
				if (pivotRow != col)
				{
					for (int c = 0; c < 4; ++c)
					{
						std::swap(a.m[col][c], a.m[pivotRow][c]);
						std::swap(inv.m[col][c], inv.m[pivotRow][c]);
					}
				}

				// Normalize pivot row.
				const float32 pivot = a.m[col][col];
				const float32 invPivot = 1.0f / pivot;
				for (int c = 0; c < 4; ++c)
				{
					a.m[col][c] *= invPivot;
					inv.m[col][c] *= invPivot;
				}

				// Eliminate other rows.
				for (int r = 0; r < 4; ++r)
				{
					if (r == col)
					{
						continue;
					}

					const float32 f = a.m[r][col];
					if (std::fabs(f) < 1e-12f)
					{
						continue;
					}

					for (int c = 0; c < 4; ++c)
					{
						a.m[r][c] -= f * a.m[col][c];
						inv.m[r][c] -= f * inv.m[col][c];
					}
				}
			}

			return inv;
		}

		inline Matrix4x4 InverseAffineFast() const
		{
			// Assumes affine: last column is [0 0 0 1]^T
			ASSERT(std::fabs(_m03) < 1e-6f && std::fabs(_m13) < 1e-6f && std::fabs(_m23) < 1e-6f);
			ASSERT(std::fabs(_m33 - 1.0f) < 1e-6f);

			const Matrix3x3 L = static_cast<Matrix3x3>(*this);
			const Matrix3x3 invL = L.Inversed();
			const Vector3 t = ExtractTranslation();

			// inv([L 0; t 1]) = [invL 0; -t*invL 1]
			const Vector3 invT = invL.MulVector(Vector3(-t.x, -t.y, -t.z));

			Matrix4x4 r = Identity();
			r._m00 = invL._m00; r._m01 = invL._m01; r._m02 = invL._m02;
			r._m10 = invL._m10; r._m11 = invL._m11; r._m12 = invL._m12;
			r._m20 = invL._m20; r._m21 = invL._m21; r._m22 = invL._m22;
			r._m30 = invT.x;   r._m31 = invT.y;   r._m32 = invT.z;
			r._m33 = 1.0f;
			return r;
		}
	};
	static_assert(sizeof(Matrix4x4) == sizeof(float32) * 16, "Matrix4x4 size is incorrect.");
	static_assert(alignof(Matrix4x4) == alignof(float32), "Matrix4x4 alignment is incorrect.");

} // namespace shz