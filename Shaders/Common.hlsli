#ifndef HLSL_COMMON_HLSLI
#define HLSL_COMMON_HLSLI

#include "HLSL_Structures.hlsli"

// Constant Buffers
cbuffer FRAME_CONSTANTS
{
	FrameConstants g_FrameCB;
};

cbuffer DRAW_CONSTANTS
{
	DrawConstants g_DrawCB;
};

//------------------------------------------------------------------------------
// Clip / UV helpers
//------------------------------------------------------------------------------
float2 ClipToUV(float4 clip)
{
	float2 ndc = clip.xy / max(clip.w, 1e-6); // [-1..1]
	return ndc * 0.5 + 0.5; // [0..1]
}

// Convert SV_Position (pixel space) to integer pixel coords.
// Note: SV_Position.xy is already in pixel units in raster space for PS.
uint2 SVPosToPixel(float4 svPos)
{
    // Use floor to map to stable pixel coordinates.
	return (uint2) floor(svPos.xy);
}

//------------------------------------------------------------------------------
// Hash / noise (break 8-frame periodic patterns)
//------------------------------------------------------------------------------
// A tiny integer hash. Good enough for temporal dithering thresholds.
uint HashU32(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

// Returns [0,1).
float Hash01(uint2 p, uint frameIndex)
{
	uint h = HashU32(p.x * 73856093u ^ p.y * 19349663u ^ frameIndex * 83492791u);
    // Keep 24 bits for stable float.
	return (float) (h & 0x00FFFFFFu) / 16777216.0f;
}

//------------------------------------------------------------------------------
// Alpha dither test
//------------------------------------------------------------------------------
// coverage: [0..1], pixel: integer pixel coords
void AlphaDitherTest(float coverage, uint2 pixel)
{
    // Use hashed threshold instead of small repeating Bayer matrix.
    // This removes obvious 8-frame periodic patterns.
	float t = Hash01(pixel, (uint) g_FrameCB.FrameIndex);
	clip(coverage - t);
}

static const int MAX_HALTON_SEQUENCE = 16;

static const float2 HALTON_SEQUENCE[MAX_HALTON_SEQUENCE] =
{
	float2(0.5, 0.333333),
	float2(0.25, 0.666667),
	float2(0.75, 0.111111),
	float2(0.125, 0.444444),
	float2(0.625, 0.777778),
	float2(0.375, 0.222222),
	float2(0.875, 0.555556),
	float2(0.0625, 0.888889),
	float2(0.5625, 0.037037),
	float2(0.3125, 0.37037),
	float2(0.8125, 0.703704),
	float2(0.1875, 0.148148),
	float2(0.6875, 0.481482),
	float2(0.4375, 0.814815),
	float2(0.9375, 0.259259),
	float2(0.03125, 0.592593)
};

float4 ApplyTAAJittering(float4 clipSpace)
{
	int idx = g_FrameCB.FrameIndex % MAX_HALTON_SEQUENCE;

	float2 jitter = HALTON_SEQUENCE[idx];
	jitter.x = ( jitter.x - 0.5f) / g_FrameCB.ViewportSize.x * 2.f;
	jitter.y = (jitter.y - 0.5f) / g_FrameCB.ViewportSize.y * 2.f;

	return clipSpace;
}

#endif // HLSL_COMMON_HLSLI
