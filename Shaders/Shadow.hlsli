#ifndef SHADOW_HLSLI
#define SHADOW_HLSLI

static float3 WorldToShadowUVZ(float3 worldPos, float4x4 lightViewProj)
{
    float4 clip = mul(float4(worldPos, 1.0), lightViewProj);
    clip.xyz /= max(clip.w, 1e-6);

    float2 uv = clip.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;

    return float3(uv, clip.z);
}

static const float2 kPoisson16[16] =
{
    float2(-0.94201624, -0.39906216),
    float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870),
    float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),
    float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554),
    float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),
    float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367),
    float2(0.14383161, -0.14100790)
};

// texelSize = 1.0 / shadowMapResolution (e.g. float2(1/2048, 1/2048))
// radiusInTexels: kernel radius (e.g. 1.5 ~ 3.0). Bigger = softer, more expensive.
float SampleShadow_PCF16(
    Texture2D<float> shadowMap,
    SamplerComparisonState shadowCmpSampler,
    float3 shadowUVZ,
    float depthBias,
    float radiusInTexels)
{
    // Early out (caller can do bounds check too)
    if (shadowUVZ.x < 0.0 || shadowUVZ.x > 1.0 || shadowUVZ.y < 0.0 || shadowUVZ.y > 1.0)
    {
        return 1.0;
    }

    uint width = 0;
    uint height = 0;
    shadowMap.GetDimensions(width, height);
    float2 texelSize = float2(1.0 / float(width), 1.0 / float(height));

    float2 uv = shadowUVZ.xy;
    float depth = shadowUVZ.z - depthBias;

    float sum = 0.0;

    // Poisson disk around uv
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float2 o = kPoisson16[i] * (texelSize * radiusInTexels);
        sum += shadowMap.SampleCmpLevelZero(shadowCmpSampler, uv + o, depth);
    }

    return sum * (1.0 / 16.0);
}
#endif // SHADOW_HLSLI
