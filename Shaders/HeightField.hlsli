#ifndef HEIGHTFIELD_HLSLI
#define HEIGHTFIELD_HLSLI

#include "HLSL_Structures.hlsli"

// ----------------------------------------------
// HeightField common helpers
// ----------------------------------------------

// Keep UV inside [0.5 texel, 1 - 0.5 texel] to avoid border filtering artifacts.
float2 HF_ClampUVToTexelCenter(float2 uv, float2 texelSize)
{
    float2 halfTexel = 0.5f * texelSize;
    return clamp(uv, halfTexel, 1.0f - halfTexel);
}

// Base mapping: worldXZ -> [0..1] UV using origin/size.
// Then apply extra scale/bias (optional).
float2 HF_WorldXZToUV(
    float2 worldXZ,
    HeightFieldConstants hf,
    float2 uvScale,
    float2 uvBias)
{
    float2 baseUV = (worldXZ - hf.WorldOriginXZ) / max(hf.WorldSizeXZ, float2(1e-6, 1e-6));
    baseUV = saturate(baseUV);

    float2 uv = baseUV * uvScale + uvBias;
    uv = saturate(uv);

    // Texel-center clamp (IMPORTANT)
    uv = HF_ClampUVToTexelCenter(uv, hf.HeightTexelSize);
    return uv;
}

float HF_SampleHeight01(Texture2D<float> heightTex, SamplerState clampSampler, float2 uv)
{
    // uv is expected already texel-center clamped
    return heightTex.SampleLevel(clampSampler, uv, 0).r;
}

float HF_SampleWorldHeight(
    Texture2D<float> heightTex,
    SamplerState clampSampler,
    float2 uv,
    HeightFieldConstants hf,
    float yOffsetMeters)
{
    float h01 = HF_SampleHeight01(heightTex, clampSampler, uv);
    return yOffsetMeters + (hf.HeightOffset + h01 * hf.HeightScale);
}

#endif // HEIGHTFIELD_HLSLI
