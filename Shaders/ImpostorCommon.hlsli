// ============================================================================
// ImpostorAtlas.hlsli 
// - Atlas: 8x8 tiles, total 64 slots, we use first 62 (0..61)
// - View indexing rule:
//   0      : top pole
//   1..60  : 5 rings * 12 azimuth (ring-major)
//   61     : bottom pole
// Rings correspond to polar angles: 30,60,90,120,150 degrees from +Y (up).
// Azimuth: 12 slices (30deg per slice), with -15deg offset and clockwise indexing.
// ============================================================================

#ifndef IMPOSTOR_COMMON_HLSLI
#define IMPOSTOR_COMMON_HLSLI

#include "Common.hlsli"

static const uint IMPOSTOR_ATLAS_DIM = 8u; // 8x8
static const uint IMPOSTOR_VIEWS = 62u; // used slots
static const uint IMPOSTOR_RINGS = 5u;
static const uint IMPOSTOR_AZIM = 12u;

float Wrap01(float x)
{
	// x -> [0,1)
	return frac(frac(x) + 1.0f);
}

uint WrapIndex(int i, uint n)
{
	int m = i % (int) n;
	if (m < 0)
		m += (int) n;
	return (uint) m;
}

float2 AtlasTileUV(float2 localUV, uint viewIndex /*0..61*/)
{
	// safety
	viewIndex = min(viewIndex, IMPOSTOR_VIEWS - 1u);

	uint tileX = viewIndex % IMPOSTOR_ATLAS_DIM;
	uint tileY = viewIndex / IMPOSTOR_ATLAS_DIM;

	float2 base = float2((float) tileX, (float) tileY);
	float2 atlasDim = float2((float) IMPOSTOR_ATLAS_DIM, (float) IMPOSTOR_ATLAS_DIM);

	return (base + localUV) / atlasDim;
}

// viewIndex helpers
uint RingAzToIndex(uint ring /*0..4*/, uint az /*0..11*/)
{
	// 1..60, ring-major
	return 1u + ring * IMPOSTOR_AZIM + az;
}

struct Impostor4
{
	uint i00;
	float w00;
	uint i10;
	float w10;
	uint i01;
	float w01;
	uint i11;
	float w11;
};

// Given viewDir in object space, choose 4 views + weights.
// localUV is NOT used here; only direction.
Impostor4 ComputeImpostorViews(float3 viewDirOS)
{
	Impostor4 o;
	o.i00 = o.i10 = o.i01 = o.i11 = 0u;
	o.w00 = 1.0f;
	o.w10 = o.w01 = o.w11 = 0.0f;

	viewDirOS = normalize(viewDirOS);

	// polar: 0 at +Y (top), PI at -Y (bottom)
	float y = clamp(viewDirOS.y, -1.0f, 1.0f);
	float polar = acos(y);

	// Pole snap thresholds (half-ring = 15deg)
	const float poleSnap = radians(15.0f);

	if (polar <= poleSnap)
	{
		// top pole
		o.i00 = 0u;
		o.w00 = 1.0f;
		return o;
	}
	if (polar >= (PI - poleSnap))
	{
		// bottom pole
		o.i00 = 61u;
		o.w00 = 1.0f;
		return o;
	}

	// Rings are at polar angles: 30,60,90,120,150 (deg)
	const float ringStart = radians(30.0f);
	const float ringStep = radians(30.0f);

	// ringPos in [0,4]
	float ringPos = (polar - ringStart) / ringStep;
	ringPos = clamp(ringPos, 0.0f, 4.0f);

	uint ring0 = (uint) floor(ringPos);
	uint ring1 = min(ring0 + 1u, IMPOSTOR_RINGS - 1u);
	float ringT = ringPos - (float) ring0;

	// azimuth around Y
	// Base yaw (CCW) = atan2(x, z). We want clockwise indexing when viewed from top -> use -yaw.
	float yawCW = atan2(viewDirOS.x, viewDirOS.z);

	// your rule: first of 12 starts at "front rotated by -15deg in Z" (clockwise -15)
	// -> we shift by +15deg so that yawCW = -15deg maps to slice 0.
	// float yawShift = yawCW + radians(15.0f);
	float yawShift = yawCW + radians(-22.5 + 180.0f);

	float az01 = Wrap01(yawShift / TWO_PI); // [0,1)
	float azPos = az01 * (float) IMPOSTOR_AZIM; // [0,12)

	uint az0 = (uint) floor(azPos) % IMPOSTOR_AZIM;
	uint az1 = (az0 + 1u) % IMPOSTOR_AZIM;
	float azT = azPos - (float) az0;

	// 2D bilinear weights (ring, az)
	float w00 = (1.0f - ringT) * (1.0f - azT);
	float w10 = (1.0f - ringT) * (azT);
	float w01 = (ringT) * (1.0f - azT);
	float w11 = (ringT) * (azT);

	o.i00 = RingAzToIndex(ring0, az0);
	o.i10 = RingAzToIndex(ring0, az1);
	o.i01 = RingAzToIndex(ring1, az0);
	o.i11 = RingAzToIndex(ring1, az1);

	o.w00 = w00;
	o.w10 = w10;
	o.w01 = w01;
	o.w11 = w11;

	return o;
}

#endif // IMPOSTOR_COMMON_HLSLI