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
// Ordered dither (Bayer 8x8) - screen-space stable
// Returns threshold in [0,1)
//------------------------------------------------------------------------------
float DitherThreshold_Bayer8(uint2 pixel)
{
    // 8x8 Bayer matrix values in [0..63]
    // Indexing: [y][x]
    static const uint B8[64] =
    {
        0, 32, 8, 40, 2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44, 4, 36, 14, 46, 6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43, 1, 33, 9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47, 7, 39, 13, 45, 5, 37,
        63, 31, 55, 23, 61, 29, 53, 21
    };

    uint x = pixel.x & 7u;
    uint y = pixel.y & 7u;
    uint idx = y * 8u + x;

    // +0.5로 cell center를 쓰면 약간 더 고르게 느껴질 때가 많음
    return ((float) B8[idx] + 0.5f) / 64.0f;
}

//------------------------------------------------------------------------------
// Alpha dither test (Bayer 8x8)
// coverage: [0..1], pixel: integer pixel coords
//------------------------------------------------------------------------------
void AlphaDitherTest_Bayer8(float coverage, uint2 pixel)
{
    float t = DitherThreshold_Bayer8(pixel); // [0,1)
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
