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

// HeightField (normalized float 0..1)
Texture2D<float> g_HeightField;

// IMPORTANT: HeightField uses CLAMP sampler (shared with grass height/interaction).
SamplerState g_LinearClampSampler;

struct VSInput
{
    float3 Pos : ATTRIB0; // x,z in [0..16], y ignored
    float2 UV : ATTRIB1; // [0..1]
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float3 WorldN : TEXCOORD2;
    float3 WorldT : TEXCOORD3;
};

void main(in VSInput IN, out VSOutput OUT)
{
    // 17x17 fixed grid: convert to [0..1]
    float2 grid01 = IN.Pos.xz * (1.0 / 16.0);

    // Chunk placement in world
    float2 worldXZ = g_TerrainDrawCB.ChunkOriginXZ + grid01 * g_TerrainDrawCB.ChunkSizeXZ;

    // Height UV: base world->uv + extra scale/bias
    float2 huv = HF_WorldXZToUV(worldXZ, g_HeightFieldCB, g_TerrainDrawCB.HeightUVScale, g_TerrainDrawCB.HeightUVBias);

    // Height decode (terrain has no YOffset)
    float wy = HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, huv, g_HeightFieldCB, 0.0f);

    float3 worldPos = float3(worldXZ.x, wy, worldXZ.y);
    OUT.WorldPos = worldPos;
    OUT.Pos = mul(float4(worldPos, 1.0), g_FrameCB.ViewProj);

    // Surface UV for material sampling
    OUT.UV = IN.UV * g_TerrainDrawCB.SurfaceUVScale + g_TerrainDrawCB.SurfaceUVBias;

    // ------------------------------------------------------------
    // Normal (central difference) using the SAME height sampling rules
    // ------------------------------------------------------------
    float stepMul = max(1.0, g_TerrainDrawCB.NormalSampleStep);

    // Move in UV space by texel size (optionally stepped)
    float2 du = float2(g_HeightFieldCB.HeightTexelSize.x * stepMul, 0.0);
    float2 dv = float2(0.0, g_HeightFieldCB.HeightTexelSize.y * stepMul);

    // Clamp to texel center again after offset
    float2 uvL = HF_ClampUVToTexelCenter(huv - du, g_HeightFieldCB.HeightTexelSize);
    float2 uvR = HF_ClampUVToTexelCenter(huv + du, g_HeightFieldCB.HeightTexelSize);
    float2 uvD = HF_ClampUVToTexelCenter(huv - dv, g_HeightFieldCB.HeightTexelSize);
    float2 uvU = HF_ClampUVToTexelCenter(huv + dv, g_HeightFieldCB.HeightTexelSize);

    float hL = HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uvL, g_HeightFieldCB, 0.0f);
    float hR = HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uvR, g_HeightFieldCB, 0.0f);
    float hD = HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uvD, g_HeightFieldCB, 0.0f);
    float hU = HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uvU, g_HeightFieldCB, 0.0f);

    // Convert to world derivatives (meters)
    float dxWorld = max(1e-6, g_HeightFieldCB.WorldSpacingXZ.x * stepMul);
    float dzWorld = max(1e-6, g_HeightFieldCB.WorldSpacingXZ.y * stepMul);

    float dHdx = (hR - hL) / (2.0 * dxWorld);
    float dHdz = (hU - hD) / (2.0 * dzWorld);

    float up = max(0.001, g_HeightFieldCB.NormalUpBias);
    float3 N = normalize(float3(-dHdx, up, -dHdz));

    // Tangent (simple, consistent)
    float3 T = normalize(float3(1.0, dHdx, 0.0));
    T = normalize(T - N * dot(N, T));

    OUT.WorldN = N;
    OUT.WorldT = T;
    
    OUT.WorldN = g_TerrainDrawCB.DebugChunkColor;
}
