#pragma once
#include "Engine/Core/Math/Public/XVector.h"
#include "Engine/Core/Math/Public/Matrix4x4.h"
#include <cmath>

namespace shz
{
	// ------------------------------------------------------------
	// XMatrix
	// - SIMD computation type
	// - Row-major storage (r0..r3 are rows)
	// - Row vector convention (v' = v * M)
	// ------------------------------------------------------------
	struct alignas(16) XMatrix
	{
		union
		{
			struct { XVector r0, r1, r2, r3; }; // rows
			XVector r[4];
		};

		static inline XMatrix Identity()
		{
			XMatrix m{};
			m.r0 = XVector::Set(1, 0, 0, 0);
			m.r1 = XVector::Set(0, 1, 0, 0);
			m.r2 = XVector::Set(0, 0, 1, 0);
			m.r3 = XVector::Set(0, 0, 0, 1);
			return m;
		}

		static inline XMatrix Load(const Matrix4x4& M)
		{
			XMatrix xm{};
			// Load rows (row-major storage)
			xm.r0 = XVector::Set(M._m00, M._m01, M._m02, M._m03);
			xm.r1 = XVector::Set(M._m10, M._m11, M._m12, M._m13);
			xm.r2 = XVector::Set(M._m20, M._m21, M._m22, M._m23);
			xm.r3 = XVector::Set(M._m30, M._m31, M._m32, M._m33);
			return xm;
		}

		inline Matrix4x4 Store() const
		{
			Vector4 a{}, b{}, c{}, d{};
			r0.Store4(a);
			r1.Store4(b);
			r2.Store4(c);
			r3.Store4(d);
			return Matrix4x4(
				a.x, a.y, a.z, a.w,
				b.x, b.y, b.z, b.w,
				c.x, c.y, c.z, c.w,
				d.x, d.y, d.z, d.w);
		}

		inline XVector MulVector(XVector v) const
		{
			// v' = v * M = v.x*r0 + v.y*r1 + v.z*r2 + v.w*r3
			const XVector xxxx = XVector::Swizzle<0x00>(v);
			const XVector yyyy = XVector::Swizzle<0x55>(v);
			const XVector zzzz = XVector::Swizzle<0xAA>(v);
			const XVector wwww = XVector::Swizzle<0xFF>(v);

			XVector out = r0 * xxxx;
			out += r1 * yyyy;
			out += r2 * zzzz;
			out += r3 * wwww;
			return out;
		}

		static inline XMatrix Mul(const XMatrix& A, const XMatrix& B)
		{
			// C = A * B. Each result row i is a linear combination of B's rows,
			// weighted by A's row-i components.
			auto MulRow = [&](XVector aRow) -> XVector
				{
					const XVector xxxx = XVector::Swizzle<0x00>(aRow);
					const XVector yyyy = XVector::Swizzle<0x55>(aRow);
					const XVector zzzz = XVector::Swizzle<0xAA>(aRow);
					const XVector wwww = XVector::Swizzle<0xFF>(aRow);
					XVector out = B.r0 * xxxx;
					out += B.r1 * yyyy;
					out += B.r2 * zzzz;
					out += B.r3 * wwww;
					return out;
				};

			XMatrix C{};
			C.r0 = MulRow(A.r0);
			C.r1 = MulRow(A.r1);
			C.r2 = MulRow(A.r2);
			C.r3 = MulRow(A.r3);
			return C;
		}

		inline XMatrix Transposed() const
		{
			Vector4 vr0{}, vr1{}, vr2{}, vr3{};
			r0.Store4(vr0);
			r1.Store4(vr1);
			r2.Store4(vr2);
			r3.Store4(vr3);

			XMatrix t{};
			t.r0 = XVector::Set(vr0.x, vr1.x, vr2.x, vr3.x);
			t.r1 = XVector::Set(vr0.y, vr1.y, vr2.y, vr3.y);
			t.r2 = XVector::Set(vr0.z, vr1.z, vr2.z, vr3.z);
			t.r3 = XVector::Set(vr0.w, vr1.w, vr2.w, vr3.w);
			return t;
		}
	};

	static_assert(sizeof(XMatrix) == sizeof(XVector) * 4);

} // namespace shz