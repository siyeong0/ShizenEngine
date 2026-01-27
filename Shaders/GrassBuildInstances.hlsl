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

// NOTE:
// - Height sampling usually wants CLAMP sampler.
// - Density sampling wants WRAP sampler for tiling.
// For simplicity we keep one sampler here (as in your current setup).
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
    // NOTE:
    // - We intentionally cast signed to unsigned. Negative values are fine; they just map to a different uint range.
    // - This is deterministic for the same inputs.
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
    // If CenterXZ=1, terrain is centered around (0,0) in XZ.
    // Otherwise origin is at (0,0).
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

// Heightmap sample in normalized [0..1]
float SampleHeightNormalized(float2 worldXZ)
{
    float2 uv;
    uv.x = WorldXToU(worldXZ.x);
    uv.y = WorldZToV(worldXZ.y);
    uv = saturate(uv);

    return g_HeightMap.SampleLevel(g_LinearWrapSampler, uv, 0.0);
}

// Height decode to world Y
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
    // contrast01 in [0..0.49]
    // - 0.0  : almost linear (no thresholding)
    // - 0.25 : smoothstep(0.25..0.75)  => mid-range emphasized
    // - 0.49 : very threshold-like (almost binary)
    float a = saturate(contrast01);
    a = min(a, 0.49f);
    return smoothstep(a, 1.0f - a, d);
}

float SampleWorldDensity(float2 worldXZ)
{
    // Uses CB tuning with safe fallbacks.
    // DensityTiling is "UV scale per meter" (worldXZ * tiling).
    // Example:
    //   tiling = 0.002  => 500m period for a 0..1 UV tile (if texture is a single tile noise).
    float tiling = (g_GrassGenCB.DensityTiling > 0.0f) ? g_GrassGenCB.DensityTiling : 0.002f;
    float contrast = (g_GrassGenCB.DensityContrast > 0.0f) ? g_GrassGenCB.DensityContrast : 0.25f;
    float powK = (g_GrassGenCB.DensityPow > 0.0f) ? g_GrassGenCB.DensityPow : 0.65f;

    float2 uv = worldXZ * tiling; // WRAP sampler -> tiled in world
    float d = g_DensityField.SampleLevel(g_LinearWrapSampler, uv, 0.0).r;

    d = saturate(d);
    d = RemapDensity(d, contrast);

    // pow < 1 boosts mid-values => patches become more visible
    d = pow(d, powK);

    return d;
}

// ----------------------------------------------------------------------------
// Slope / Height masks
// ----------------------------------------------------------------------------
float ComputeSlope01(float2 worldXZ)
{
    // NOTE:
    // We estimate slope by central differences on the *normalized* heightmap,
    // then convert to world-space slope using HeightScale and spacing.
    //
    // The resulting 's' is approximately tan(theta). We scale it by SlopeToDensity
    // and clamp to [0..1] to produce a usable mask.
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
    // Height range is expressed in normalized heightmap space [0..1]
    // - HeightMinN/MaxN define the allowed band
    // - HeightFadeN softens edges (smoothstep)
    float hMinN = g_GrassGenCB.HeightMinN;
    float hMaxN = g_GrassGenCB.HeightMaxN;
    float hFadeN = max(g_GrassGenCB.HeightFadeN, 1e-6);

    // Safety: if unset (0/0), allow all heights by default
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
    // Conservative test:
    // If AABB is fully outside any plane, cull it.
    // We choose the "positive vertex" with respect to the plane normal.
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
        {
            return false;
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Chunk-based generation
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void GenerateGrassInstances(uint3 tid : SV_DispatchThreadID)
{
    // Thread -> chunk offset around camera
    int halfExt = g_GrassGenCB.ChunkHalfExtent;
    int2 chunkGrid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);

    float chunkSize = g_GrassGenCB.ChunkSize;

    int2 camChunk = int2(floor(camXZ / max(chunkSize, 1e-6)));
    int2 worldChunk = camChunk + chunkGrid;

    float2 chunkOriginXZ = float2(worldChunk) * chunkSize;

    // Chunk frustum culling (early out)
    //
    // NOTE:
    // We don't have min/max terrain height per chunk yet, so we use a conservative +/- 20m band.
    // If you later precompute per-chunk height range (min/max), plug it here to tighten culling.
    float chunkOriginHeight = SampleWorldHeight(chunkOriginXZ);

    float3 chunkMin = float3(chunkOriginXZ.x,
                             chunkOriginHeight - 20.0,
                             chunkOriginXZ.y);

    float3 chunkMax = float3(chunkOriginXZ.x + chunkSize,
                             chunkOriginHeight + 20.0,
                             chunkOriginXZ.y + chunkSize);

    if (!AabbInsideFrustum(chunkMin, chunkMax))
    {
        return;
    }

    // Chunk seed: deterministic for the same chunk coordinate + salt
    uint chunkSeed = Hash2i(worldChunk, g_GrassGenCB.SeedSalt);

    [loop]
    for (uint s = 0; s < g_GrassGenCB.SamplesPerChunk; ++s)
    {
        uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

        // Random point inside chunk (world XZ) with jitter
        float ux = Rand01(seed ^ 0x2222u);
        float uz = Rand01(seed ^ 0x3333u);

        float jx = (ux - 0.5f) * g_GrassGenCB.Jitter;
        float jz = (uz - 0.5f) * g_GrassGenCB.Jitter;

        float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * chunkSize;
        float2 posXZ = chunkOriginXZ + localXZ;

        // Radius culling (camera-centered window)
        float2 dc = posXZ - camXZ;
        if (dot(dc, dc) > g_GrassGenCB.SpawnRadius * g_GrassGenCB.SpawnRadius)
        {
            continue;
        }

        // Density base (WORLD-TILED)
        float density = SampleWorldDensity(posXZ);
        if (density <= 0.001f)
        {
            continue;
        }

        // Slope/height masks
        float hN = SampleHeightNormalized(posXZ); // 0..1
        float slope01 = ComputeSlope01(posXZ); // 0..1-ish

        // NOTE:
        // - slope01 is "steepness"
        // - we want "flatness" as a multiplier (steep => fewer instances)
        float slopeMask = 1.0f - slope01;
        float heightMask = ComputeHeightMask(hN);

        density *= saturate(slopeMask) * heightMask;

        if (density <= 0.001f)
        {
            continue;
        }

        // IMPORTANT: density-gated attempts
        //
        // Without this, if SamplesPerChunk is large, low density regions still
        // produce too many attempts and the final result looks "averaged out".
        // This gate makes density affect *how often we even try* to spawn.
        if (Rand01(seed ^ 0x41A7u) > density)
        {
            continue;
        }

        // Spawn probability (fine control)
        float effectiveProb = saturate(g_GrassGenCB.SpawnProb * density);

        // Separate tap to reduce correlation with jitter / density gate
        if (Rand01(seed ^ 0x4444u) > effectiveProb)
        {
            continue;
        }

        // Allocate instance slot (atomic)
        uint idx;
        g_Counter.InterlockedAdd(0, 1, idx);

        if (idx >= MAX_INSTANCES)
        {
            return;
        }

        // Height (world)
        float y = SampleWorldHeight(posXZ);

        GrassInstance inst;
        inst.PosWS = float3(posXZ.x, y, posXZ.y);

        // Scale
        //
        // NOTE:
        // densityScaleBias makes sparse areas look less empty by slightly increasing blade size.
        // This is an artistic choice; if you want physically consistent density, set it to 1.0.
        float scaleT = Rand01(seed ^ 0x5555u);
        float densityScaleBias = lerp(1.2f, 0.8f, density); // low density -> bigger

        // NOTE:
        // Keep *0.04f if your grass mesh authoring scale is large.
        inst.Scale =
            lerp(g_GrassGenCB.MinScale, g_GrassGenCB.MaxScale, scaleT) *
            0.04f *
            densityScaleBias;

        // Orientation
        inst.Yaw = Rand01(seed ^ 0x6666u) * 6.2831853f;

        // Small initial lean (radians)
        inst.Pitch = lerp(-0.90f, 0.90f, Rand01(seed ^ 0x7777u));

        // Bend stiffness (wind response factor)
        inst.BendStrength =
            lerp(g_GrassGenCB.BendStrengthMin,
                 g_GrassGenCB.BendStrengthMax,
                 Rand01(seed ^ 0x8888u));

        inst._pad0 = 0;

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
