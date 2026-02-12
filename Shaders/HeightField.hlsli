#ifndef HEIGHTFIELD_HLSLI
#define HEIGHTFIELD_HLSLI

#include "HLSL_Structures.hlsli"

cbuffer HEIGHT_FIELD_CONSTANTS
{
	HeightFieldConstants g_HeightFieldCB;
};

// ----------------------------------------------
// HeightField common helpers
// ----------------------------------------------

// Keep UV inside [0.5 texel, 1 - 0.5 texel] to avoid border filtering artifacts.
float2 HF_ClampUVToTexelCenter(float2 uv, float2 texelSize)
{
    float2 halfTexel = 0.5f * texelSize;
    return clamp(uv, halfTexel, 1.0f - halfTexel);
}

// WorldXZ -> grid coords in "height texel space" (0..Width-1, 0..Height-1)
float2 HF_WorldXZToGrid(float2 worldXZ)
{
	return (worldXZ - g_HeightFieldCB.WorldOriginXZ) / max(g_HeightFieldCB.WorldSpacingXZ, 1e-6.xx);
}

// Grid -> UV, sampling at texel centers.
// uv = (grid + 0.5) / (Width, Height) == (grid + 0.5) * texelSize
float2 HF_GridToUV(float2 gridXZ)
{
	return (gridXZ + 0.5.xx) * g_HeightFieldCB.HeightTexelSize;
}

// Spacing-aware mapping.
// 1) world -> grid (spacing)
// 2) grid -> uv (texel center)
// 3) extra scale/bias (optional)
// 4) texel-center clamp (avoid border filtering)
float2 HF_WorldXZToUV(
    float2 worldXZ,
    float2 uvScale,
    float2 uvBias)
{
    float2 grid = HF_WorldXZToGrid(worldXZ);

    // Clamp grid into valid texel index range so we don't sample outside.
    // (Width-1, Height-1) in grid space is (WorldSize / Spacing).
	float2 gridMax = max((g_HeightFieldCB.WorldSizeXZ / max(g_HeightFieldCB.WorldSpacingXZ, 1e-6.xx)), 0.0.xx);
    grid = clamp(grid, 0.0.xx, gridMax);

    float2 uv = HF_GridToUV(grid);
    uv = uv * uvScale + uvBias;

    uv = saturate(uv);
	uv = HF_ClampUVToTexelCenter(uv, g_HeightFieldCB.HeightTexelSize);
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
    float yOffsetMeters)
{
    float h01 = HF_SampleHeight01(heightTex, clampSampler, uv);
	return yOffsetMeters + (g_HeightFieldCB.HeightOffset + h01 * g_HeightFieldCB.HeightScale);
}

float HF_SampleWorldHeightAtWorldXZ(
    Texture2D<float> heightTex,
    SamplerState clampSampler,
    float2 worldXZ,
    float2 uvScale,
    float2 uvBias,
    float yOffsetMeters)
{
    float2 uv = HF_WorldXZToUV(worldXZ, uvScale, uvBias);
    return HF_SampleWorldHeight(heightTex, clampSampler, uv, yOffsetMeters);
}


#endif // HEIGHTFIELD_HLSLI
