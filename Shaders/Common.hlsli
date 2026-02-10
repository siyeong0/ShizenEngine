#ifndef HLSL_COMMON_HLSLI
#define HLSL_COMMON_HLSLI

#include "HLSL_Structures.hlsli"

struct BaseVSOutput
{
    float4 SVPosition   : SV_POSITION;
    float4 CurrClip     : TEXCOORD0;
    float4 PrevClip     : TEXCOORD1;
    float2 UV           : TEXCOORD2;
    float3 WorldPosition : TEXCOORD3;
    float3 WorldNormal   : TEXCOORD4;
    float3 WorldTangent  : TEXCOORD5;
};

struct BasePSInput
{
    float4 SVPosition   : SV_POSITION;
    float4 CurrClip     : TEXCOORD0;
    float4 PrevClip     : TEXCOORD1;
    float2 UV           : TEXCOORD2;
    float3 WorldPosition : TEXCOORD3;
    float3 WorldNormal   : TEXCOORD4;
    float3 WorldTangent  : TEXCOORD5;
	
    bool bFrontFace : SV_IsFrontFace;
};


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
    return (uint2) floor(svPos.xy);
}

//------------------------------------------------------------------------------
// UE-like DitherTemporalAA style threshold
//  - DO NOT move pattern in screen space (no pix shifting)
//  - Time variation comes from scrambling/permutation only
//------------------------------------------------------------------------------
uint Hash_u32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Threshold in [0..1) from 4x4 Bayer, time-scrambled without moving pixels.
// This avoids "pattern crawling/shimmer" from frame-based pixel shifts.
//float DitherThreshold4x4(int2 pix)
//{
//    static const float bayer4[16] =
//    {
//        0, 8, 2, 10,
//        12, 4, 14, 6,
//        3, 11, 1, 9,
//        15, 7, 13, 5
//    };

//    // fixed 4x4 cell
//    int idx = (pix.x & 3) + ((pix.y & 3) << 2);

//    // time-dependent permutation of 0..15
//    uint f = (uint) g_FrameCB.FrameIndex;
//    uint r = Hash_u32(f * 0x9e3779b9u + (uint) idx);
//    int j = (int) (r & 15u);

//    return (bayer4[j] + 0.5) / 16.0; // (0..1)
//}
float DitherThreshold4x4(int2 pix)
{
    static const float bayer4[16] =
    {
        0, 8, 2, 10,
        12, 4, 14, 6,
        3, 11, 1, 9,
        15, 7, 13, 5
    };

    int idx = (pix.x & 3) + ((pix.y & 3) << 2);
    return (bayer4[idx] + 0.5) / 16.0;
}

//------------------------------------------------------------------------------
// TAA Jitter (Halton)
//------------------------------------------------------------------------------
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
    int idx = (int) (g_FrameCB.FrameIndex % MAX_HALTON_SEQUENCE);

    float2 jitter = HALTON_SEQUENCE[idx];
    jitter.x = (jitter.x - 0.5f) / g_FrameCB.ViewportSize.x * 2.f;
    jitter.y = (jitter.y - 0.5f) / g_FrameCB.ViewportSize.y * 2.f;

    clipSpace.xy += jitter;
    return clipSpace;
}

#endif // HLSL_COMMON_HLSLI