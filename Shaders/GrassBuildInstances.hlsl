#include "HLSL_Structures.hlsli"

// Constant Buffers
cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer GRASS_GEN_CONSTANTS
{
    GrassGenConstants g_GrassGenCB;
};

// Outputs
RWStructuredBuffer<GrassInstance> g_OutInstances;

// 20 bytes: D3D12_DRAW_INDEXED_ARGUMENTS layout
RWByteAddressBuffer g_IndirectArgs;

// uint Counter (4 bytes) at byte offset 0
RWByteAddressBuffer g_Counter;

// Instance cap (must match buffer capacity on CPU side)
static const uint MAX_INSTANCES = 1u << 24;

// Heightmap (R16_UNORM sampled as normalized float 0..1)
Texture2D<float> g_HeightMap;

// Density field (recommend: R8_UNORM, grayscale, tiled in world space)
Texture2D<float> g_DensityField;

// Interaction field (0..1). 1 = heavily pressed.
Texture2D<float> g_InteractionField;

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
// Terrain mapping (world XZ -> heightmap UV)
// ----------------------------------------------------------------------------
float GetTerrainSizeX()
{
    return float(max((int) g_GrassGenCB.HFWidth - 1, 0)) * g_GrassGenCB.SpacingX;
}

float GetTerrainSizeZ()
{
    return float(max((int) g_GrassGenCB.HFHeight - 1, 0)) * g_GrassGenCB.SpacingZ;
}

float GetTerrainOriginX()
{
    return (g_GrassGenCB.CenterXZ != 0) ? (-0.5 * GetTerrainSizeX()) : 0.0;
}

float GetTerrainOriginZ()
{
    return (g_GrassGenCB.CenterXZ != 0) ? (-0.5 * GetTerrainSizeZ()) : 0.0;
}

float WorldXToU(float worldX)
{
    float sizeX = max(GetTerrainSizeX(), 1e-6);
    return (worldX - GetTerrainOriginX()) / sizeX;
}

float WorldZToV(float worldZ)
{
    float sizeZ = max(GetTerrainSizeZ(), 1e-6);
    return (worldZ - GetTerrainOriginZ()) / sizeZ;
}

float SampleHeightNormalized(float2 worldXZ)
{
    float2 uv;
    uv.x = WorldXToU(worldXZ.x);
    uv.y = WorldZToV(worldXZ.y);
    uv = saturate(uv);

    return g_HeightMap.SampleLevel(g_LinearWrapSampler, uv, 0.0);
}

float SampleWorldHeight(float2 worldXZ)
{
    float hN = SampleHeightNormalized(worldXZ);
    return g_GrassGenCB.YOffset + (g_GrassGenCB.HeightOffset + hN * g_GrassGenCB.HeightScale);
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
// Interaction sampling (terrain-mapped)
// ----------------------------------------------------------------------------
float SampleInteraction(float2 worldXZ)
{
    float2 uv;
    uv.x = WorldXToU(worldXZ.x);
    uv.y = WorldZToV(worldXZ.y);
    uv = saturate(uv);

    return g_InteractionField.SampleLevel(g_LinearWrapSampler, uv, 0.0).r; // 0..1
}

// ----------------------------------------------------------------------------
// Slope / Height masks
// ----------------------------------------------------------------------------
float ComputeSlope01(float2 worldXZ)
{
    float2 e = float2(g_GrassGenCB.SpacingX, g_GrassGenCB.SpacingZ);

    float hX1 = SampleHeightNormalized(worldXZ + float2(e.x, 0));
    float hX0 = SampleHeightNormalized(worldXZ - float2(e.x, 0));
    float hZ1 = SampleHeightNormalized(worldXZ + float2(0, e.y));
    float hZ0 = SampleHeightNormalized(worldXZ - float2(0, e.y));

    float dhdx = (hX1 - hX0) * g_GrassGenCB.HeightScale / max(2.0f * e.x, 1e-6);
    float dhdz = (hZ1 - hZ0) * g_GrassGenCB.HeightScale / max(2.0f * e.y, 1e-6);

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
// Chunk-based generation
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void GenerateGrassInstances(uint3 tid : SV_DispatchThreadID)
{
    int halfExt = g_GrassGenCB.ChunkHalfExtent;
    int2 chunkGrid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
    float chunkSize = g_GrassGenCB.ChunkSize;

    int2 camChunk = int2(floor(camXZ / max(chunkSize, 1e-6)));
    int2 worldChunk = camChunk + chunkGrid;

    float2 chunkOriginXZ = float2(worldChunk) * chunkSize;

    float chunkOriginHeight = SampleWorldHeight(chunkOriginXZ);

    float3 chunkMin = float3(chunkOriginXZ.x,
                             chunkOriginHeight - 20.0,
                             chunkOriginXZ.y);

    float3 chunkMax = float3(chunkOriginXZ.x + chunkSize,
                             chunkOriginHeight + 20.0,
                             chunkOriginXZ.y + chunkSize);

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

        float2 dc = posXZ - camXZ;
        if (dot(dc, dc) > g_GrassGenCB.SpawnRadius * g_GrassGenCB.SpawnRadius)
            continue;

        // Base density
        float density = SampleWorldDensity(posXZ);
        if (density <= 0.001f)
            continue;

        // Slope/height masks
        float hN = SampleHeightNormalized(posXZ);
        float slope01 = ComputeSlope01(posXZ);

        float slopeMask = 1.0f - slope01;
        float heightMask = ComputeHeightMask(hN);

        density *= saturate(slopeMask) * heightMask;
        if (density <= 0.001f)
            continue;

        // --- Interaction: sample ONCE per instance attempt ---
        float press = SampleInteraction(posXZ); // 0..1
        press = saturate(press);

        if (density <= 0.001f)
            continue;

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
        inst.Pitch = lerp(-0.90f, 0.90f, Rand01(seed ^ 0x7777u));

        inst.BendStrength =
            lerp(g_GrassGenCB.BendStrengthMin,
                 g_GrassGenCB.BendStrengthMax,
                 Rand01(seed ^ 0x8888u));

        // Store interaction pressure for VS (NO texture sampling in VS)
        inst.Press = press;

        g_OutInstances[idx] = inst;
    }
}


// ----------------------------------------------------------------------------
// Indirect args
// ----------------------------------------------------------------------------
static const uint INDEX_COUNT_PER_INSTANCE = 39;
static const uint START_INDEX_LOCATION = 0;
static const uint BASE_VERTEX_LOCATION = 0;
static const uint START_INSTANCE_LOCATION = 0;

[numthreads(1, 1, 1)]
void WriteIndirectArgs(uint3 tid : SV_DispatchThreadID)
{
    uint instanceCount = g_Counter.Load(0);
    instanceCount = min(instanceCount, MAX_INSTANCES);

    // D3D12_DRAW_INDEXED_ARGUMENTS:
    //   uint IndexCountPerInstance
    //   uint InstanceCount
    //   uint StartIndexLocation
    //   int  BaseVertexLocation
    //   uint StartInstanceLocation
    g_IndirectArgs.Store(0, INDEX_COUNT_PER_INSTANCE);
    g_IndirectArgs.Store(4, instanceCount);
    g_IndirectArgs.Store(8, START_INDEX_LOCATION);
    g_IndirectArgs.Store(12, BASE_VERTEX_LOCATION);
    g_IndirectArgs.Store(16, START_INSTANCE_LOCATION);
}
