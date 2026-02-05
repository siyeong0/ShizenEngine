#include "HLSL_Structures.hlsli"
#include "HeightField.hlsli"

// Constant Buffers
cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer HEIGHT_FIELD_CONSTANTS
{
    HeightFieldConstants g_HeightFieldCB;
};

cbuffer GRASS_GEN_CONSTANTS
{
    GrassGenConstants g_GrassGenCB;
};

// Outputs
RWStructuredBuffer<GrassInstance> g_OutInstances;

// uint Counter (4 bytes) at byte offset 0
RWByteAddressBuffer g_Counter;

// Instance cap (must match buffer capacity on CPU side)
static const uint MAX_INSTANCES = 1u << 24;

// HeightField (R16_UNORM sampled as normalized float 0..1)
Texture2D<float> g_HeightField;

// Density field (recommend: R8_UNORM, grayscale, tiled in world space)
Texture2D<float> g_DensityField;

// Interaction field (0..1). 1 = heavily pressed.
Texture2D<float> g_InteractionField;

// Samplers
SamplerState g_LinearClampSampler;
SamplerState g_LinearWrapSampler;

// ----------------------------------------------------------------------------
// Random / Hash
// ----------------------------------------------------------------------------
uint WangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16);
    seed *= 9u;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15);
    return seed;
}

float Rand01(uint seed)
{
    return (WangHash(seed) & 0x00FFFFFFu) / 16777216.0f;
}

uint Hash2i(int2 v, uint salt)
{
    uint x = (uint) v.x;
    uint y = (uint) v.y;
    return (x * 73856093u) ^ (y * 19349663u) ^ salt;
}

// ----------------------------------------------------------------------------
// Density sampling (world-tiled) + remap curve
// ----------------------------------------------------------------------------
float RemapDensity(float d, float contrast01)
{
    float a = saturate(contrast01);
    a = min(a, 0.49f);
    return smoothstep(a, 1.0f - a, d);
}

float SampleWorldDensity(float2 worldXZ)
{
    float tiling = (g_GrassGenCB.DensityTiling > 0.0f) ? g_GrassGenCB.DensityTiling : 0.002f;
    float contrast = (g_GrassGenCB.DensityContrast > 0.0f) ? g_GrassGenCB.DensityContrast : 0.25f;
    float powK = (g_GrassGenCB.DensityPow > 0.0f) ? g_GrassGenCB.DensityPow : 0.65f;

    float2 uv = worldXZ * tiling;
    float d = g_DensityField.SampleLevel(g_LinearWrapSampler, uv, 0.0).r;

    d = saturate(d);
    d = RemapDensity(d, contrast);
    d = pow(d, powK);

    return d;
}

// ----------------------------------------------------------------------------
// Height/Interaction sampling (SHARED mapping rules with Terrain.vsh)
// ----------------------------------------------------------------------------
float2 WorldXZToHeightUV(float2 worldXZ)
{
    // Grass uses pure base mapping: scale=1, bias=0
    return HF_WorldXZToUV(worldXZ, g_HeightFieldCB, float2(1.0, 1.0), float2(0.0, 0.0));
}

float SampleHeightNormalized(float2 worldXZ)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    return HF_SampleHeight01(g_HeightField, g_LinearClampSampler, uv);
}

float SampleWorldHeight(float2 worldXZ)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    return HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uv, g_HeightFieldCB, g_GrassGenCB.YOffset);
}

float SampleInteraction(float2 worldXZ)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    return g_InteractionField.SampleLevel(g_LinearClampSampler, uv, 0.0).r;
}

// ----------------------------------------------------------------------------
// Slope / Height masks (uses SAME height sampling rules)
// ----------------------------------------------------------------------------
float ComputeSlope01(float2 worldXZ)
{
    float2 e = g_HeightFieldCB.WorldSpacingXZ;

    // NOTE: slope uses 1-texel spacing steps (world meters)
    float hX1 = SampleHeightNormalized(worldXZ + float2(e.x, 0));
    float hX0 = SampleHeightNormalized(worldXZ - float2(e.x, 0));
    float hZ1 = SampleHeightNormalized(worldXZ + float2(0, e.y));
    float hZ0 = SampleHeightNormalized(worldXZ - float2(0, e.y));

    float dhdx = (hX1 - hX0) * g_HeightFieldCB.HeightScale / max(2.0f * e.x, 1e-6);
    float dhdz = (hZ1 - hZ0) * g_HeightFieldCB.HeightScale / max(2.0f * e.y, 1e-6);

    float s = length(float2(dhdx, dhdz)); // ~tan(theta)

    float slopeTo01 = (g_GrassGenCB.SlopeToDensity > 0.0f) ? g_GrassGenCB.SlopeToDensity : 0.15f;
    return saturate(s * slopeTo01);
}

float ComputeHeightMask(float hN)
{
    float hMinN = g_GrassGenCB.HeightMinN;
    float hMaxN = g_GrassGenCB.HeightMaxN;
    float hFadeN = max(g_GrassGenCB.HeightFadeN, 1e-6);

    if (hMaxN <= hMinN)
    {
        hMinN = 0.0f;
        hMaxN = 1.0f;
        hFadeN = 0.03f;
    }

    float a = smoothstep(hMinN, hMinN + hFadeN, hN);
    float b = 1.0f - smoothstep(hMaxN - hFadeN, hMaxN, hN);

    return saturate(a * b);
}

// ----------------------------------------------------------------------------
// Frustum culling (AABB vs 6 planes)
// ----------------------------------------------------------------------------
bool AabbInsideFrustum(float3 bmin, float3 bmax)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float4 P = g_FrameCB.FrustumPlanesWS[i];
        float3 n = P.xyz;
        float d = P.w;

        float3 v;
        v.x = (n.x >= 0.0) ? bmax.x : bmin.x;
        v.y = (n.y >= 0.0) ? bmax.y : bmin.y;
        v.z = (n.z >= 0.0) ? bmax.z : bmin.z;

        if (dot(n, v) + d < 0.0)
            return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Chunk grid mapping (HeightField-space aligned)
// ----------------------------------------------------------------------------
// IMPORTANT:
// We align grass chunks in the SAME world space as the terrain heightfield:
//
// - Heightfield world origin: g_HeightFieldCB.WorldOriginXZ
// - Heightfield spacing:      g_HeightFieldCB.WorldSpacingXZ
// - Heightfield extents:      [origin .. origin+WorldSize]
//
// Grass chunk origin is computed relative to WorldOriginXZ, not absolute 0,
// so centerXZ terrain (negative origin) stays perfectly aligned.
int2 WorldXZToChunkCoord(float2 worldXZ, float chunkSize)
{
    float2 rel = worldXZ - g_HeightFieldCB.WorldOriginXZ;
    float inv = rcp(max(chunkSize, 1e-6));
    float2 c = floor(rel * inv);
    return int2((int) c.x, (int) c.y);
}

float2 ChunkCoordToWorldOrigin(int2 chunkCoord, float chunkSize)
{
    return g_HeightFieldCB.WorldOriginXZ + float2(chunkCoord) * chunkSize;
}

// Clamp generated chunk origin so we don't spawn outside heightfield coverage.
// (Optional but recommended; prevents ¡°edge smear¡± when camera near bounds)
bool ClampChunkToHeightfield(inout float2 chunkOriginXZ, float chunkSize)
{
    float2 hfMin = g_HeightFieldCB.WorldOriginXZ;
    float2 hfMax = g_HeightFieldCB.WorldOriginXZ + g_HeightFieldCB.WorldSizeXZ;

    // we want [origin .. origin+chunkSize] to intersect hf bounds
    float2 o = chunkOriginXZ;
    float2 e = o + chunkSize.xx;

    // if completely outside, reject
    if (e.x < hfMin.x || e.y < hfMin.y || o.x > hfMax.x || o.y > hfMax.y)
        return false;

    // clamp origin so sampling stays within a reasonable range
    // NOTE: we don't force full containment; just keep origin near bounds.
    chunkOriginXZ = clamp(chunkOriginXZ, hfMin - chunkSize.xx, hfMax);
    return true;
}

// ----------------------------------------------------------------------------
// Chunk-based generation
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void GenerateGrassInstances(uint3 tid : SV_DispatchThreadID)
{
    int halfExt = g_GrassGenCB.ChunkHalfExtent;
    int2 chunkGrid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
    float chunkSize = g_GrassGenCB.ChunkSize;

    // Heightfield-aligned chunk coord around camera
    int2 camChunk = WorldXZToChunkCoord(camXZ, chunkSize);
    int2 worldChunk = camChunk + chunkGrid;

    float2 chunkOriginXZ = ChunkCoordToWorldOrigin(worldChunk, chunkSize);

    // Optional clamp/reject outside bounds
    if (!ClampChunkToHeightfield(chunkOriginXZ, chunkSize))
        return;

    // Conservative vertical bounds: sample one height and expand range
    float chunkOriginHeight = SampleWorldHeight(chunkOriginXZ);

    float3 chunkMin = float3(chunkOriginXZ.x, chunkOriginHeight - 20.0, chunkOriginXZ.y);
    float3 chunkMax = float3(chunkOriginXZ.x + chunkSize, chunkOriginHeight + 20.0, chunkOriginXZ.y + chunkSize);

    if (!AabbInsideFrustum(chunkMin, chunkMax))
        return;

    uint chunkSeed = Hash2i(worldChunk, g_GrassGenCB.SeedSalt);

    [loop]
    for (uint s = 0; s < g_GrassGenCB.SamplesPerChunk; ++s)
    {
        uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

        float ux = Rand01(seed ^ 0x2222u);
        float uz = Rand01(seed ^ 0x3333u);

        float jx = (ux - 0.5f) * g_GrassGenCB.Jitter;
        float jz = (uz - 0.5f) * g_GrassGenCB.Jitter;

        float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * chunkSize;
        float2 posXZ = chunkOriginXZ + localXZ;

        // Spawn radius (camera-local)
        float2 dc = posXZ - camXZ;
        if (dot(dc, dc) > g_GrassGenCB.SpawnRadius * g_GrassGenCB.SpawnRadius)
            continue;

        // Base density (tiled)
        float density = SampleWorldDensity(posXZ);
        if (density <= 0.001f)
            continue;

        // Slope/height masks (height sampling is unified)
        float hN = SampleHeightNormalized(posXZ);
        float slope01 = ComputeSlope01(posXZ);

        float slopeMask = 1.0f - slope01;
        float heightMask = ComputeHeightMask(hN);

        density *= saturate(slopeMask) * heightMask;
        if (density <= 0.001f)
            continue;

        // Interaction (unified UV/sampler with height)
        float press = saturate(SampleInteraction(posXZ));

        // Density-gated attempts
        if (Rand01(seed ^ 0x41A7u) > density)
            continue;

        float effectiveProb = saturate(g_GrassGenCB.SpawnProb * density);
        if (Rand01(seed ^ 0x4444u) > effectiveProb)
            continue;

        uint idx;
        g_Counter.InterlockedAdd(0, 1, idx);

        if (idx >= MAX_INSTANCES)
            return;

        float y = SampleWorldHeight(posXZ);

        GrassInstance inst;
        inst.PosWS = float3(posXZ.x, y, posXZ.y);

        float scaleT = Rand01(seed ^ 0x5555u);
        float densityScaleBias = lerp(1.2f, 0.8f, density);

        inst.Scale =
            lerp(g_GrassGenCB.MinScale, g_GrassGenCB.MaxScale, scaleT) *
            0.04f *
            densityScaleBias;

        inst.Yaw = Rand01(seed ^ 0x6666u) * 6.2831853f;
        inst.Pitch = lerp(g_GrassGenCB.MinPitch, g_GrassGenCB.MaxPitch, Rand01(seed ^ 0x7777u));

        inst.BendStrength =
            lerp(g_GrassGenCB.BendStrengthMin,
                 g_GrassGenCB.BendStrengthMax,
                 Rand01(seed ^ 0x8888u));

        inst.Press = press;

        g_OutInstances[idx] = inst;
    }
}
