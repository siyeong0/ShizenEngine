#include "HLSL_Structures.hlsli"
#include "HeightField.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer HEIGHT_FIELD_CONSTANTS
{
    HeightFieldConstants g_HeightFieldCB;
};

cbuffer TERRAIN_DRAW_CONSTANTS
{
    TerrainDrawConstants g_TerrainDrawCB;
};

Texture2D<float> g_HeightField;
SamplerState g_LinearClampSampler;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
    float3 Normal : ATTRIB2;
    float3 Tangent : ATTRIB3;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0; // surface uv (world 0..1 over entire terrain, then scale/bias)
    float3 WorldPos : TEXCOORD1;
    float3 WorldN : TEXCOORD2;
    float3 WorldT : TEXCOORD3;
};

static float2 snapWorldXZ(float2 worldXZ, HeightFieldConstants hf, float stepMul)
{
    float2 cell = hf.WorldSpacingXZ * stepMul;
    float2 g = (worldXZ - hf.WorldOriginXZ) / max(cell, 1e-6.xx);
    float2 gi = floor(g + 0.5.xx);
    return hf.WorldOriginXZ + gi * cell;
}

static float sampleWorldHeightAt(float2 worldXZ)
{
    return HF_SampleWorldHeightAtWorldXZ(
        g_HeightField,
        g_LinearClampSampler,
        worldXZ,
        g_HeightFieldCB,
        g_TerrainDrawCB.HeightUVScale,
        g_TerrainDrawCB.HeightUVBias,
        0.0);
}

static float3 computeNormalAt(float2 worldXZ, float stepMul)
{
    float2 spacing = g_HeightFieldCB.WorldSpacingXZ * stepMul;

    float2 dx = float2(spacing.x, 0.0);
    float2 dz = float2(0.0, spacing.y);

    float hL = sampleWorldHeightAt(worldXZ - dx);
    float hR = sampleWorldHeightAt(worldXZ + dx);
    float hD = sampleWorldHeightAt(worldXZ - dz);
    float hU = sampleWorldHeightAt(worldXZ + dz);

    float dHdx = (hR - hL) / max(2.0 * spacing.x, 1e-6);
    float dHdz = (hU - hD) / max(2.0 * spacing.y, 1e-6);

    float up = max(0.001, g_HeightFieldCB.NormalUpBias);
    return normalize(float3(-dHdx, up, -dHdz));
}

void main(in VSInput IN, out VSOutput OUT)
{
    // Local grid [0..1] within this chunk
    float2 grid01 = IN.Pos.xz * (1.0 / 16.0);

    // Chunk-local -> world XZ (meters)
    float2 worldXZ = g_TerrainDrawCB.ChunkOriginXZ + grid01 * g_TerrainDrawCB.ChunkSizeXZ;

    // LOD morph
    float stepFine = max(1.0, g_TerrainDrawCB.NormalSampleStep);
    float stepCoarse = stepFine * 2.0;

    float alpha = saturate(g_TerrainDrawCB.LodMorphAlpha);
    if (g_TerrainDrawCB.LodIndex >= 4)
        alpha = 0.0;

    float hFine = sampleWorldHeightAt(worldXZ);

    float2 worldXZCoarse = snapWorldXZ(worldXZ, g_HeightFieldCB, stepCoarse);
    float hCoarse = sampleWorldHeightAt(worldXZCoarse);

    float wy = lerp(hCoarse, hFine, alpha);

    float3 worldPos = float3(worldXZ.x, wy, worldXZ.y);
    OUT.WorldPos = worldPos;

    OUT.Pos = mul(float4(worldPos, 1.0), g_FrameCB.ViewProj);

    // Surface UV: entire terrain mapped to [0..1] then optional tiling
    float2 uvWorld01 = (worldXZ - g_HeightFieldCB.WorldOriginXZ) / max(g_HeightFieldCB.WorldSizeXZ, 1e-6.xx);
    OUT.UV = uvWorld01 * g_TerrainDrawCB.SurfaceUVScale + g_TerrainDrawCB.SurfaceUVBias;

    // Normal + tangent
    float3 NFine = computeNormalAt(worldXZ, stepFine);
    float3 NCoarse = computeNormalAt(worldXZCoarse, stepCoarse);
    float3 N = normalize(lerp(NCoarse, NFine, alpha));

    float3 T = float3(1.0, 0.0, 0.0);
    T = normalize(T - N * dot(N, T));

    OUT.WorldN = N;
    OUT.WorldT = T;
}
