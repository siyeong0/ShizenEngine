#pragma once
#include "Engine/Core/Math/Public/Constants.h"

// -------------------------------------------
// Backend selection
// -------------------------------------------
#if defined(SHZ_FORCE_NO_SSE)
#define SHZ_HAS_SSE 0
#else
#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || defined(__SSE__) || defined(__SSE2__)
#define SHZ_HAS_SSE 1
#else
#define SHZ_HAS_SSE 0
#endif
#endif

#if SHZ_HAS_SSE
#include "Engine/Core/Math/Public/XVector_SSE.h"
#else

namespace shz
{
	struct Vector2;
	struct Vector3;
	struct Vector4;

	// ------------------------------------------------------------
	// XVector (FPU fallback)
	// - Public API MUST match XVector_SSE.h exactly.
	// - Mask lanes: 0xFFFFFFFF (true) or 0x00000000 (false).
	// ------------------------------------------------------------
	struct alignas(16) XVector
	{
	private:
		union
		{
			float32  e[4];
			uint32_t u[4];
		};
	public:
		// --------------------------------------------------------
		// Constructors
		// --------------------------------------------------------
		inline XVector() = default;
		inline XVector(float32 x, float32 y, float32 z, float32 w)
		{
			e[0] = x; e[1] = y; e[2] = z; e[3] = w;
		}

		// --------------------------------------------------------
		// Basic creators
		// --------------------------------------------------------
		static inline XVector Zero() { return XVector(0.f, 0.f, 0.f, 0.f); }
		static inline XVector One() { return XVector(1.f, 1.f, 1.f, 1.f); }
		static inline XVector Set(float32 x, float32 y, float32 z, float32 w) { return XVector(x, y, z, w); }
		static inline XVector Splat(float32 s) { return XVector(s, s, s, s); }

		// --------------------------------------------------------
		// Load (signatures match SSE header)
		// --------------------------------------------------------
		static inline XVector Load4(const Vector4& a)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return Set(p[0], p[1], p[2], p[3]);
		}

		static inline XVector Load3(const Vector3& a, const float32 w = 0.f)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return Set(p[0], p[1], p[2], w);
		}

		static inline XVector Load3Pos(const Vector3& a)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return Set(p[0], p[1], p[2], 1.0f);
		}

		static inline XVector Load3Dir(const Vector3& a)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return Set(p[0], p[1], p[2], 0.0f);
		}

		static inline XVector Load2(const Vector2& a, const float32 z = 0.f, const float32 w = 0.f)
		{
			const float32* p = reinterpret_cast<const float32*>(&a);
			return Set(p[0], p[1], z, w);
		}

		// --------------------------------------------------------
		// Store (signatures match SSE header)
		// --------------------------------------------------------
		inline void Store4(Vector4& out) const
		{
			float32* p = reinterpret_cast<float32*>(&out);
			p[0] = e[0]; p[1] = e[1]; p[2] = e[2]; p[3] = e[3];
		}

		inline void Store3(Vector3& out) const
		{
			float32* p = reinterpret_cast<float32*>(&out);
			p[0] = e[0]; p[1] = e[1]; p[2] = e[2];
		}

		inline void Store2(Vector2& out) const
		{
			float32* p = reinterpret_cast<float32*>(&out);
			p[0] = e[0]; p[1] = e[1];
		}

		// Optional raw stores (useful for tests/interop)
		inline void Store4(float32* out4) const
		{
			out4[0] = e[0]; out4[1] = e[1]; out4[2] = e[2]; out4[3] = e[3];
		}

		// --------------------------------------------------------
		// Explicit arithmetic helpers (requested)
		// --------------------------------------------------------
		static inline XVector Add(XVector a, XVector b) { return a + b; }
		static inline XVector Sub(XVector a, XVector b) { return a - b; }
		static inline XVector Mul(XVector a, XVector b) { return a * b; }
		static inline XVector Div(XVector a, XVector b) { return a / b; }

		// --------------------------------------------------------
		// Operators (must match SSE header)
		// --------------------------------------------------------
		inline XVector operator+(XVector rhs) const { return Set(e[0] + rhs.e[0], e[1] + rhs.e[1], e[2] + rhs.e[2], e[3] + rhs.e[3]); }
		inline XVector operator-(XVector rhs) const { return Set(e[0] - rhs.e[0], e[1] - rhs.e[1], e[2] - rhs.e[2], e[3] - rhs.e[3]); }
		inline XVector operator*(XVector rhs) const { return Set(e[0] * rhs.e[0], e[1] * rhs.e[1], e[2] * rhs.e[2], e[3] * rhs.e[3]); }
		inline XVector operator/(XVector rhs) const { return Set(e[0] / rhs.e[0], e[1] / rhs.e[1], e[2] / rhs.e[2], e[3] / rhs.e[3]); }

		inline XVector operator*(float32 s) const { return Set(e[0] * s, e[1] * s, e[2] * s, e[3] * s); }
		inline XVector operator/(float32 s) const { return Set(e[0] / s, e[1] / s, e[2] / s, e[3] / s); }

		inline XVector& operator+=(XVector rhs) { *this = *this + rhs; return *this; }
		inline XVector& operator-=(XVector rhs) { *this = *this - rhs; return *this; }
		inline XVector& operator*=(XVector rhs) { *this = *this * rhs; return *this; }
		inline XVector& operator/=(XVector rhs) { *this = *this / rhs; return *this; }

		inline XVector& operator*=(float32 s) { *this = *this * s; return *this; }
		inline XVector& operator/=(float32 s) { *this = *this / s; return *this; }

		friend inline XVector operator*(float32 s, const XVector& v) { return v * s; }

		// --------------------------------------------------------
		// Bitwise ops (must behave like SSE masks)
		// --------------------------------------------------------
		static inline XVector And(XVector a, XVector b) { return fromBits(a.u[0] & b.u[0], a.u[1] & b.u[1], a.u[2] & b.u[2], a.u[3] & b.u[3]); }
		static inline XVector Or(XVector a, XVector b) { return fromBits(a.u[0] | b.u[0], a.u[1] | b.u[1], a.u[2] | b.u[2], a.u[3] | b.u[3]); }
		static inline XVector Xor(XVector a, XVector b) { return fromBits(a.u[0] ^ b.u[0], a.u[1] ^ b.u[1], a.u[2] ^ b.u[2], a.u[3] ^ b.u[3]); }
		static inline XVector Not(XVector a) { return fromBits(~a.u[0], ~a.u[1], ~a.u[2], ~a.u[3]); }

		// --------------------------------------------------------
		// Min/Max/Abs/Negate/Clamp/Saturate
		// --------------------------------------------------------
		static inline XVector Min(XVector a, XVector b)
		{
			return Set(
				(a.e[0] < b.e[0]) ? a.e[0] : b.e[0],
				(a.e[1] < b.e[1]) ? a.e[1] : b.e[1],
				(a.e[2] < b.e[2]) ? a.e[2] : b.e[2],
				(a.e[3] < b.e[3]) ? a.e[3] : b.e[3]
			);
		}

		static inline XVector Max(XVector a, XVector b)
		{
			return Set(
				(a.e[0] > b.e[0]) ? a.e[0] : b.e[0],
				(a.e[1] > b.e[1]) ? a.e[1] : b.e[1],
				(a.e[2] > b.e[2]) ? a.e[2] : b.e[2],
				(a.e[3] > b.e[3]) ? a.e[3] : b.e[3]
			);
		}

		static inline XVector Abs(XVector a)
		{
			// clear sign bit
			return fromBits(a.u[0] & 0x7FFFFFFFu, a.u[1] & 0x7FFFFFFFu, a.u[2] & 0x7FFFFFFFu, a.u[3] & 0x7FFFFFFFu);
		}

		static inline XVector Negate(XVector a)
		{
			return Set(-a.e[0], -a.e[1], -a.e[2], -a.e[3]);
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
			return Vector4::MinComponent(Vector4(e[0], e[1], e[2], e[3]));
		}

		inline float32 Max4() const
		{
			return Vector4::MaxComponent(Vector4(e[0], e[1], e[2], e[3]));
		}

		inline float32 Sum4() const
		{
			return e[0] + e[1] + e[2] + e[3];
		}

		inline float32 Min3() const
		{
			return Vector3::MinComponent(Vector3(e[0], e[1], e[2]));
		}

		inline float32 Max3() const
		{
			return Vector3::MaxComponent(Vector3(e[0], e[1], e[2]));
		}

		inline float32 Sum3() const
		{
			return e[0] + e[1] + e[2];
		}

		// --------------------------------------------------------
		// Reciprocal / Rsqrt (Est versions are precise here)
		// --------------------------------------------------------
		static inline XVector ReciprocalEst(XVector x) { return Reciprocal(x); }
		static inline XVector Reciprocal(XVector x)
		{
			return Set(1.0f / x.e[0], 1.0f / x.e[1], 1.0f / x.e[2], 1.0f / x.e[3]);
		}

		static inline XVector RsqrtEst(XVector x) { return Rsqrt(x); }
		static inline XVector Rsqrt(XVector x)
		{
			return Set(
				1.0f / std::sqrt(x.e[0]),
				1.0f / std::sqrt(x.e[1]),
				1.0f / std::sqrt(x.e[2]),
				1.0f / std::sqrt(x.e[3])
			);
		}

		static inline XVector Sqrt(XVector x)
		{
			return Set(std::sqrt(x.e[0]), std::sqrt(x.e[1]), std::sqrt(x.e[2]), std::sqrt(x.e[3]));
		}

		// --------------------------------------------------------
		// Comparisons (return masks: 0xFFFFFFFF / 0)
		// --------------------------------------------------------
		static inline XVector CompareEQ(XVector a, XVector b)
		{
			return fromBits(
				(a.e[0] == b.e[0]) ? 0xFFFFFFFFu : 0u,
				(a.e[1] == b.e[1]) ? 0xFFFFFFFFu : 0u,
				(a.e[2] == b.e[2]) ? 0xFFFFFFFFu : 0u,
				(a.e[3] == b.e[3]) ? 0xFFFFFFFFu : 0u
			);
		}

		static inline XVector CompareNE(XVector a, XVector b)
		{
			return fromBits(
				(a.e[0] != b.e[0]) ? 0xFFFFFFFFu : 0u,
				(a.e[1] != b.e[1]) ? 0xFFFFFFFFu : 0u,
				(a.e[2] != b.e[2]) ? 0xFFFFFFFFu : 0u,
				(a.e[3] != b.e[3]) ? 0xFFFFFFFFu : 0u
			);
		}

		static inline XVector CompareLT(XVector a, XVector b)
		{
			return fromBits(
				(a.e[0] < b.e[0]) ? 0xFFFFFFFFu : 0u,
				(a.e[1] < b.e[1]) ? 0xFFFFFFFFu : 0u,
				(a.e[2] < b.e[2]) ? 0xFFFFFFFFu : 0u,
				(a.e[3] < b.e[3]) ? 0xFFFFFFFFu : 0u
			);
		}

		static inline XVector CompareLE(XVector a, XVector b)
		{
			return fromBits(
				(a.e[0] <= b.e[0]) ? 0xFFFFFFFFu : 0u,
				(a.e[1] <= b.e[1]) ? 0xFFFFFFFFu : 0u,
				(a.e[2] <= b.e[2]) ? 0xFFFFFFFFu : 0u,
				(a.e[3] <= b.e[3]) ? 0xFFFFFFFFu : 0u
			);
		}

		static inline XVector CompareGT(XVector a, XVector b)
		{
			return fromBits(
				(a.e[0] > b.e[0]) ? 0xFFFFFFFFu : 0u,
				(a.e[1] > b.e[1]) ? 0xFFFFFFFFu : 0u,
				(a.e[2] > b.e[2]) ? 0xFFFFFFFFu : 0u,
				(a.e[3] > b.e[3]) ? 0xFFFFFFFFu : 0u
			);
		}

		static inline XVector CompareGE(XVector a, XVector b)
		{
			return fromBits(
				(a.e[0] >= b.e[0]) ? 0xFFFFFFFFu : 0u,
				(a.e[1] >= b.e[1]) ? 0xFFFFFFFFu : 0u,
				(a.e[2] >= b.e[2]) ? 0xFFFFFFFFu : 0u,
				(a.e[3] >= b.e[3]) ? 0xFFFFFFFFu : 0u
			);
		}

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
			// (mask & b) | (~mask & a)
			return Or(And(mask, b), And(Not(mask), a));
		}

		static inline int MoveMask(XVector mask)
		{
			int m = 0;
			m |= int((mask.u[0] >> 31) & 1u) << 0;
			m |= int((mask.u[1] >> 31) & 1u) << 1;
			m |= int((mask.u[2] >> 31) & 1u) << 2;
			m |= int((mask.u[3] >> 31) & 1u) << 3;
			return m;
		}

		static inline bool AnyTrue(XVector mask) { return MoveMask(mask) != 0; }
		static inline bool AllTrue(XVector mask) { return MoveMask(mask) == 0xF; }

		// --------------------------------------------------------
		// Shuffle / swizzle (signature must match SSE: template<uint8_t Imm>)
		// imm encoding matches _mm_shuffle_ps
		// - result = { a[w], a[x], b[y], b[z] }? (SSE semantics: [a[x], a[y], b[z], b[w]])
		// We'll match SSE semantics used in your SSE header: _mm_shuffle_ps(a,b,Imm)
		// -> lanes: [a[imm&3], a[(imm>>2)&3], b[(imm>>4)&3], b[(imm>>6)&3]]
		// --------------------------------------------------------
		template<uint8_t Imm>
		static inline XVector Shuffle(XVector a, XVector b)
		{
			static_assert(Imm < 256);
			const int x = (Imm >> 0) & 3;
			const int y = (Imm >> 2) & 3;
			const int z = (Imm >> 4) & 3;
			const int w = (Imm >> 6) & 3;
			return Set(a.e[x], a.e[y], b.e[z], b.e[w]);
		}

		template<uint8_t Imm>
		static inline XVector Swizzle(XVector a)
		{
			static_assert(Imm < 256);
			return Shuffle<Imm>(a, a);
		}

		// --------------------------------------------------------
		// Dot / length
		// --------------------------------------------------------
		static inline float32 Dot4(XVector a, XVector b) { return a.e[0] * b.e[0] + a.e[1] * b.e[1] + a.e[2] * b.e[2] + a.e[3] * b.e[3]; }
		static inline float32 Dot3(XVector a, XVector b) { return a.e[0] * b.e[0] + a.e[1] * b.e[1] + a.e[2] * b.e[2]; }

		static inline XVector Dot4V(XVector a, XVector b) { float32 d = Dot4(a, b); return Splat(d); }
		static inline XVector Dot3V(XVector a, XVector b) { float32 d = Dot3(a, b); return Splat(d); }

		static inline float32 Length4(XVector a) { return std::sqrt(Dot4(a, a)); }
		static inline float32 Length3(XVector a) { return std::sqrt(Dot3(a, a)); }

		// --------------------------------------------------------
		// Cross / normalize
		// --------------------------------------------------------
		static inline XVector Cross3(XVector a, XVector b)
		{
			// assumes xyz vectors; w is preserved as 0 in common usage
			return Set(
				a.e[1] * b.e[2] - a.e[2] * b.e[1],
				a.e[2] * b.e[0] - a.e[0] * b.e[2],
				a.e[0] * b.e[1] - a.e[1] * b.e[0],
				0.0f
			);
		}

		static inline XVector Normalize3(XVector a)
		{
			const float32 len = Length3(a);
			return Set(a.e[0] / len, a.e[1] / len, a.e[2] / len, 0.0f);
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
		static inline XVector fromBits(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
		{
			XVector r;
			r.u[0] = x; r.u[1] = y; r.u[2] = z; r.u[3] = w;
			return r;
		}
	};

	static_assert(sizeof(XVector) == 16, "XVector size mismatch");
	static_assert(alignof(XVector) == 16, "XVector alignment mismatch");
} // namespace shz

#endif // SHZ_HAS_SSE