#pragma once
#include "Engine/Core/Math/Public/Constants.h"

#if !defined(SHZ_FORCE_NO_SSE)
#include <xmmintrin.h>
#include <emmintrin.h>
#if defined(__SSE4_1__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <smmintrin.h>
#endif
#endif

namespace shz
{
	// Forward declarations of your engine types
	struct Vector2;
	struct Vector3;
	struct Vector4;

	// ------------------------------------------------------------
	// XVector (SSE backend)
	// - Public signature MUST match the non-SSE fallback.
	// ------------------------------------------------------------
	struct alignas(16) XVector
	{
	private:
		__m128 v;
	public:
		// --------------------------------------------------------
		// Constructors
		// --------------------------------------------------------
		inline XVector() = default;
		inline XVector(float32 x, float32 y, float32 z, float32 w) : v(_mm_set_ps(w, z, y, x)) {}

		// --------------------------------------------------------
		// Basic creators
		// --------------------------------------------------------
		static inline XVector Zero() { return fromM128(_mm_setzero_ps()); }
		static inline XVector One() { return fromM128(_mm_set1_ps(1.0f)); }

		static inline XVector Set(float32 x, float32 y, float32 z, float32 w)
		{
			return fromM128(_mm_set_ps(w, z, y, x));
		}

		static inline XVector Splat(float32 s)
		{
			return fromM128(_mm_set1_ps(s));
		}

		// --------------------------------------------------------
		// Load
		// --------------------------------------------------------
		static inline XVector Load4(const Vector4& a)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return fromM128(_mm_loadu_ps(p));
		}

		static inline XVector Load3(const Vector3& a, const float32 w = 0.f)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return fromM128(_mm_set_ps(w, p[2], p[1], p[0]));
		}

		static inline XVector Load3Pos(const Vector3& a)
		{
			return Load3(a, 1.0f);
		}

		static inline XVector Load3Dir(const Vector3& a)
		{
			return Load3(a, 0.0f);
		}

		static inline XVector Load2(const Vector2& a, const float32 z = 0.f, const float32 w = 0.f)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return fromM128(_mm_set_ps(w, z, p[1], p[0]));
		}

		// --------------------------------------------------------
		// Store 
		// --------------------------------------------------------
		inline void Store4(Vector4& out) const
		{
			float32* p = reinterpret_cast<float32*>(&out);
			_mm_storeu_ps(p, v);
		}

		inline void Store3(Vector3& out) const
		{
			// store xyz only
			float32 tmp[4];
			_mm_storeu_ps(tmp, v);
			float32* p = reinterpret_cast<float32*>(&out);
			p[0] = tmp[0]; p[1] = tmp[1]; p[2] = tmp[2];
		}

		inline void Store2(Vector2& out) const
		{
			float32 tmp[4];
			_mm_storeu_ps(tmp, v);
			float32* p = reinterpret_cast<float32*>(&out);
			p[0] = tmp[0]; p[1] = tmp[1];
		}

		inline void Store4(float32* out4) const
		{
			_mm_storeu_ps(out4, v);
		}

		// --------------------------------------------------------
		// Explicit arithmetic helpers (requested)
		// --------------------------------------------------------
		static inline XVector Add(XVector a, XVector b) { return a + b; }
		static inline XVector Sub(XVector a, XVector b) { return a - b; }
		static inline XVector Mul(XVector a, XVector b) { return a * b; }
		static inline XVector Div(XVector a, XVector b) { return a / b; }

		// --------------------------------------------------------
		// Operators (existing)
		// --------------------------------------------------------
		inline XVector operator+(XVector rhs) const { return fromM128(_mm_add_ps(v, rhs.v)); }
		inline XVector operator-(XVector rhs) const { return fromM128(_mm_sub_ps(v, rhs.v)); }
		inline XVector operator*(XVector rhs) const { return fromM128(_mm_mul_ps(v, rhs.v)); }
		inline XVector operator/(XVector rhs) const { return fromM128(_mm_div_ps(v, rhs.v)); }

		inline XVector operator*(float32 s) const { return fromM128(_mm_mul_ps(v, _mm_set1_ps(s))); }
		inline XVector operator/(float32 s) const { return fromM128(_mm_div_ps(v, _mm_set1_ps(s))); }

		inline XVector& operator+=(XVector rhs) { v = _mm_add_ps(v, rhs.v); return *this; }
		inline XVector& operator-=(XVector rhs) { v = _mm_sub_ps(v, rhs.v); return *this; }
		inline XVector& operator*=(XVector rhs) { v = _mm_mul_ps(v, rhs.v); return *this; }
		inline XVector& operator/=(XVector rhs) { v = _mm_div_ps(v, rhs.v); return *this; }

		inline XVector& operator*=(float32 s) { v = _mm_mul_ps(v, _mm_set1_ps(s)); return *this; }
		inline XVector& operator/=(float32 s) { v = _mm_div_ps(v, _mm_set1_ps(s)); return *this; }

		friend inline XVector operator*(float32 s, const XVector& v) { return v * s; }

		// --------------------------------------------------------
		// Bitwise ops
		// --------------------------------------------------------
		static inline XVector And(XVector a, XVector b) { return fromM128(_mm_and_ps(a.v, b.v)); }
		static inline XVector Or(XVector a, XVector b) { return fromM128(_mm_or_ps(a.v, b.v)); }
		static inline XVector Xor(XVector a, XVector b) { return fromM128(_mm_xor_ps(a.v, b.v)); }
		static inline XVector Not(XVector a) { return fromM128(_mm_xor_ps(a.v, allOnes())); }

		// --------------------------------------------------------
		// Min/Max/Abs/Negate/Clamp/Saturate
		// --------------------------------------------------------
		static inline XVector Min(XVector a, XVector b) { return fromM128(_mm_min_ps(a.v, b.v)); }
		static inline XVector Max(XVector a, XVector b) { return fromM128(_mm_max_ps(a.v, b.v)); }

		static inline XVector Abs(XVector a)
		{
			// clear sign bit
			const __m128 m = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
			return fromM128(_mm_and_ps(a.v, m));
		}

		static inline XVector Negate(XVector a)
		{
			const __m128 sign = _mm_castsi128_ps(_mm_set1_epi32(0x80000000));
			return fromM128(_mm_xor_ps(a.v, sign));
		}

		static inline XVector Clamp(XVector v, XVector lo, XVector hi)
		{
			return Min(Max(v, lo), hi);
		}

		static inline XVector Saturate(XVector v)
		{
			return Clamp(v, Zero(), One());
		}

		// --------------------------------------------------------
		// Reductions
		// --------------------------------------------------------
		inline float32 Min4() const
		{
			__m128 t = v;
			__m128 shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1)); // (y,x,w,z)
			t = _mm_min_ps(t, shuf);
			shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2));        // swap pairs
			t = _mm_min_ps(t, shuf);
			return _mm_cvtss_f32(t);
		}

		inline float32 Max4() const
		{
			__m128 t = v;
			__m128 shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1));
			t = _mm_max_ps(t, shuf);
			shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2));
			t = _mm_max_ps(t, shuf);
			return _mm_cvtss_f32(t);
		}

		inline float32 Sum4() const
		{
			// sum = x+y+z+w
			__m128 t = v;
			__m128 shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1)); // (y,x,w,z)
			t = _mm_add_ps(t, shuf);                                     // (x+y, y+x, z+w, w+z)
			shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2));        // swap pairs
			t = _mm_add_ps(t, shuf);                                     // (x+y+z+w, ...)
			return _mm_cvtss_f32(t);
		}

		inline float32 Min3() const
		{
			// Ignore w by setting it to +INF (neutral for min)
			const __m128 inf = _mm_set1_ps(std::numeric_limits<float32>::infinity());
			const __m128 maskW = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0)); // lane3 all ones, others 0
			__m128 t = v;
			t = _mm_or_ps(_mm_and_ps(maskW, inf), _mm_andnot_ps(maskW, t));    // set w=+inf
			__m128 shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1));
			t = _mm_min_ps(t, shuf);
			shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2));
			t = _mm_min_ps(t, shuf);
			return _mm_cvtss_f32(t);
		}

		inline float32 Max3() const
		{
			// Ignore w by setting it to -INF (neutral for max)
			const __m128 ninf = _mm_set1_ps(-std::numeric_limits<float32>::infinity());
			const __m128 maskW = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
			__m128 t = v;
			t = _mm_or_ps(_mm_and_ps(maskW, ninf), _mm_andnot_ps(maskW, t));   // set w=-inf
			__m128 shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1));
			t = _mm_max_ps(t, shuf);
			shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2));
			t = _mm_max_ps(t, shuf);
			return _mm_cvtss_f32(t);
		}

		inline float32 Sum3() const
		{
			// Ignore w by setting it to 0
			const __m128 zero = _mm_setzero_ps();
			const __m128 maskW = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
			__m128 t = v;
			t = _mm_or_ps(_mm_and_ps(maskW, zero), _mm_andnot_ps(maskW, t));   // set w=0
			__m128 shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1));
			t = _mm_add_ps(t, shuf);
			shuf = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2));
			t = _mm_add_ps(t, shuf);
			return _mm_cvtss_f32(t);
		}

		// --------------------------------------------------------
		// Reciprocal / Rsqrt / Sqrt
		// --------------------------------------------------------
		static inline XVector ReciprocalEst(XVector x) { return fromM128(_mm_rcp_ps(x.v)); }
		static inline XVector Reciprocal(XVector x) { return fromM128(_mm_div_ps(_mm_set1_ps(1.0f), x.v)); }

		static inline XVector RsqrtEst(XVector x) { return fromM128(_mm_rsqrt_ps(x.v)); }
		static inline XVector Rsqrt(XVector x) { return fromM128(_mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(x.v))); }

		static inline XVector Sqrt(XVector x) { return fromM128(_mm_sqrt_ps(x.v)); }

		// --------------------------------------------------------
		// Comparisons (return masks)
		// --------------------------------------------------------
		static inline XVector CompareEQ(XVector a, XVector b) { return fromM128(_mm_cmpeq_ps(a.v, b.v)); }
		static inline XVector CompareNE(XVector a, XVector b) { return fromM128(_mm_cmpneq_ps(a.v, b.v)); }
		static inline XVector CompareLT(XVector a, XVector b) { return fromM128(_mm_cmplt_ps(a.v, b.v)); }
		static inline XVector CompareLE(XVector a, XVector b) { return fromM128(_mm_cmple_ps(a.v, b.v)); }
		static inline XVector CompareGT(XVector a, XVector b) { return fromM128(_mm_cmpgt_ps(a.v, b.v)); }
		static inline XVector CompareGE(XVector a, XVector b) { return fromM128(_mm_cmpge_ps(a.v, b.v)); }

		static inline XVector NearEqual(XVector a, XVector b, float32 epsilon)
		{
			const XVector d = Abs(a - b);
			return CompareLE(d, Splat(epsilon));
		}

		// --------------------------------------------------------
		// Select / masks
		// --------------------------------------------------------
		static inline XVector Select(XVector a, XVector b, XVector mask)
		{
#if defined(__SSE4_1__)
			// mask selects from b when mask lane MSB is set.
			return fromM128(_mm_blendv_ps(a.v, b.v, mask.v));
#else
			return Or(And(mask, b), And(Not(mask), a));
#endif
		}

		static inline int MoveMask(XVector mask)
		{
			return _mm_movemask_ps(mask.v);
		}

		static inline bool AnyTrue(XVector mask) { return MoveMask(mask) != 0; }
		static inline bool AllTrue(XVector mask) { return MoveMask(mask) == 0xF; }

		// --------------------------------------------------------
		// Shuffle / swizzle
		// - imm encoding matches _mm_shuffle_ps.
		// --------------------------------------------------------
		template<uint8_t Imm>
		static inline XVector Shuffle(XVector a, XVector b)
		{
			static_assert(Imm < 256);
			return fromM128(_mm_shuffle_ps(a.v, b.v, Imm));
		}

		template<uint8_t Imm>
		static inline XVector Swizzle(XVector a)
		{
			static_assert(Imm < 256);
			return fromM128(_mm_shuffle_ps(a.v, a.v, Imm));
		}

		// --------------------------------------------------------
		// Dot / length
		// --------------------------------------------------------
		static inline float32 Dot4(XVector a, XVector b)
		{
#if defined(__SSE4_1__)
			const __m128 d = _mm_dp_ps(a.v, b.v, 0xFF);
			return _mm_cvtss_f32(d);
#else
			__m128 m = _mm_mul_ps(a.v, b.v);
			__m128 shuf = _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 3, 0, 1));
			__m128 sums = _mm_add_ps(m, shuf);
			shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(1, 0, 3, 2));
			sums = _mm_add_ps(sums, shuf);
			return _mm_cvtss_f32(sums);
#endif
		}

		static inline float32 Dot3(XVector a, XVector b)
		{
#if defined(__SSE4_1__)
			const __m128 d = _mm_dp_ps(a.v, b.v, 0x7F); // xyz
			return _mm_cvtss_f32(d);
#else
			__m128 av = _mm_and_ps(a.v, maskXYZ());
			__m128 bv = _mm_and_ps(b.v, maskXYZ());
			return Dot4(fromM128(av), fromM128(bv));
#endif
		}

		static inline XVector Dot4V(XVector a, XVector b) { return Splat(Dot4(a, b)); }
		static inline XVector Dot3V(XVector a, XVector b) { return Splat(Dot3(a, b)); }

		static inline float32 Length4(XVector a) { return std::sqrt(Dot4(a, a)); }
		static inline float32 Length3(XVector a) { return std::sqrt(Dot3(a, a)); }

		// --------------------------------------------------------
		// Cross / normalize
		// --------------------------------------------------------
		static inline XVector Cross3(XVector a, XVector b)
		{
			// cross(a.xyz, b.xyz), w = 0
			const __m128 a_yzx = _mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(3, 0, 2, 1));
			const __m128 b_zxy = _mm_shuffle_ps(b.v, b.v, _MM_SHUFFLE(3, 1, 0, 2));
			const __m128 a_zxy = _mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(3, 1, 0, 2));
			const __m128 b_yzx = _mm_shuffle_ps(b.v, b.v, _MM_SHUFFLE(3, 0, 2, 1));
			const __m128 c = _mm_sub_ps(_mm_mul_ps(a_yzx, b_zxy), _mm_mul_ps(a_zxy, b_yzx));
			return fromM128(_mm_and_ps(c, maskXYZ()));
		}

		static inline XVector Normalize3(XVector a)
		{
			const float32 len = Length3(a);
			// Divide all lanes then clear w
			const __m128 invLen = _mm_set1_ps(1.0f / len);
			__m128 n = _mm_mul_ps(a.v, invLen);
			// force w = 0
			n = _mm_and_ps(n, maskXYZ());
			return fromM128(n);
		}

		static inline XVector Normalize4(XVector a)
		{
			const float32 len = Length4(a);
			return a / len;
		}

		static inline XVector Normalize(XVector a)
		{
			return Normalize4(a);
		}

		// --------------------------------------------------------
		// Lerp
		// --------------------------------------------------------
		static inline XVector Lerp(XVector a, XVector b, float32 t)
		{
			return a + (b - a) * t;
		}

		static inline XVector LerpV(XVector a, XVector b, XVector t)
		{
			return a + (b - a) * t;
		}

	private:
		// --------------------------------------------------------
		// Internal helpers
		// --------------------------------------------------------
		static inline __m128 allOnes()
		{
			const __m128i m = _mm_set1_epi32(-1);
			return _mm_castsi128_ps(m);
		}

		static inline __m128 maskXYZ()
		{
			// mask = {x=1, y=1, z=1, w=0}
			const __m128i m = _mm_set_epi32(0, -1, -1, -1);
			return _mm_castsi128_ps(m);
		}

		static inline XVector fromM128(__m128 x) { XVector r; r.v = x; return r; }

	};

	static_assert(sizeof(XVector) == 16, "XVector size mismatch");
	static_assert(alignof(XVector) == 16, "XVector alignment mismatch");

} // namespace shz