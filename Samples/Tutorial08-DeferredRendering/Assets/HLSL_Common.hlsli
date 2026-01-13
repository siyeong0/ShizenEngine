#include "HLSL_Structures.hlsli"

#ifndef TUTORIAL08_COMMON_HLSLI
#define TUTORIAL08_COMMON_HLSLI

static const float PI = 3.14159265359f;

// ----------------------------------------------
// Fullscreen vertex output
// ----------------------------------------------
struct FullscreenVSOut
{
    float4 Pos : SV_POSITION;
    float2 UV : TEX_COORD;
};

// ----------------------------------------------
// Helpers
// ----------------------------------------------
float2 GetScreenUVFromSVPos(float2 svPosXY, float2 invViewportSize)
{
    return svPosXY * invViewportSize;
}

float3 GetNDCFromSVPos(float4 svPos, float depth01, float2 invViewportSize)
{
    float2 uv = GetScreenUVFromSVPos(svPos.xy, invViewportSize);
    float2 ndcXY = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float ndcZ = depth01; // D3D/VK: 0..1
    return float3(ndcXY, ndcZ);
}

float3 ReconstructWorldPos(float4 svPos, float depth01, float2 invViewportSize, float4x4 viewProjInv)
{
    float3 ndc = GetNDCFromSVPos(svPos, depth01, invViewportSize);
    float4 clip = float4(ndc, 1.0);

    float4 world = mul(clip, viewProjInv);
    world.xyz /= max(world.w, 1e-8);
    return world.xyz;
}

float3 EncodeNormal(float3 n)
{
    n = normalize(n);
    return n * 0.5 + 0.5;
}

float3 DecodeNormal(float3 enc)
{
    return normalize(enc * 2.0 - 1.0);
}

// Convert NDC z to depth01 (D3D/VK 0..1 assumed)
float NdcZToDepth01(float ndcZ)
{
    return ndcZ;
}

// Shadow projection & sampling
float2 ShadowNDCToUV(float2 ndcXY)
{
    // NDC: x,y in [-1,1], y=+1 top
    // Texture UV: (0,0) top-left
    float2 uv = ndcXY * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}

float ComputeShadowFactorPCF(
    Texture2D<float> ShadowMap,
    SamplerComparisonState ShadowMap_sampler,

    float3 worldPos,
    float4x4 lightViewProj,

    float2 shadowMapTexelSize,
    float shadowBias,
    float shadowStrength
)
{
    float4 lclip = mul(float4(worldPos, 1.0), lightViewProj);
    float3 lndc = lclip.xyz / max(lclip.w, 1e-8);

    float2 uv = ShadowNDCToUV(lndc.xy);
    float depth = NdcZToDepth01(lndc.z) - shadowBias;

    [branch]
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;

    float sum = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * shadowMapTexelSize;
            sum += ShadowMap.SampleCmpLevelZero(ShadowMap_sampler, uv + offset, depth);
        }
    }

    float pcf = sum / 9.0;
    return lerp(1.0, pcf, saturate(shadowStrength));
}

// Simple tonemap (ACES approx)
float3 ToneMapACES(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 LinearToGamma(float3 c)
{
    return pow(max(c, 0.0), 1.0 / 2.2);
}

#endif // TUTORIAL08_COMMON_HLSLI
