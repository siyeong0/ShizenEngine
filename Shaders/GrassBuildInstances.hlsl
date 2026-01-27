#include "HLSL_Structures.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer GRASS_GEN_CONSTANTS
{
    GrassGenConstants g_GrassGenCB;
};

RWStructuredBuffer<GrassInstance> g_OutInstances;

// 20 bytes: D3D12_DRAW_INDEXED_ARGUMENTS layout
RWByteAddressBuffer g_IndirectArgs;

// uint Counter (4 bytes) at byte offset 0
RWByteAddressBuffer g_Counter;

static const uint kMaxInstances = 1u << 24;

// Heightmap (R16_UNORM sampled as normalized float 0..1)
Texture2D<float> g_HeightMap;

// Density field (recommend: R8_UNORM, grayscale)
Texture2D<float> g_DensityField;

// NOTE:
// - Height sampling usually wants CLAMP sampler.
// - Density sampling wants WRAP sampler for tiling.
// For simplicity we keep one sampler here (as in your original code).
SamplerState g_LinearWrapSampler;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
uint wang_hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16);
    seed *= 9u;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15);
    return seed;
}

float rand01(uint seed)
{
    return (wang_hash(seed) & 0x00FFFFFFu) / 16777216.0f;
}

uint hash2i(int2 v, uint salt)
{
    uint x = (uint) v.x;
    uint y = (uint) v.y;
    return (x * 73856093u) ^ (y * 19349663u) ^ salt;
}

// ------------------------------------------------------------
// Terrain mapping (from CB)
// ------------------------------------------------------------
float GetSizeX()
{
    return float(max((int) g_GrassGenCB.HFWidth - 1, 0)) * g_GrassGenCB.SpacingX;
}

float GetSizeZ()
{
    return float(max((int) g_GrassGenCB.HFHeight - 1, 0)) * g_GrassGenCB.SpacingZ;
}

float GetOriginX()
{
    return (g_GrassGenCB.CenterXZ != 0) ? (-0.5 * GetSizeX()) : 0.0;
}

float GetOriginZ()
{
    return (g_GrassGenCB.CenterXZ != 0) ? (-0.5 * GetSizeZ()) : 0.0;
}

float WorldXZToU(float worldX)
{
    float sizeX = max(GetSizeX(), 1e-6);
    return (worldX - GetOriginX()) / sizeX;
}

float WorldXZToV(float worldZ)
{
    float sizeZ = max(GetSizeZ(), 1e-6);
    return (worldZ - GetOriginZ()) / sizeZ;
}

// Heightmap sample in normalized 0..1
float SampleHeightNormalized(float2 worldXZ)
{
    float2 uv;
    uv.x = WorldXZToU(worldXZ.x);
    uv.y = WorldXZToV(worldXZ.y);
    uv = saturate(uv);

    return g_HeightMap.SampleLevel(g_LinearWrapSampler, uv, 0.0);
}

float SampleWorldHeight(float2 worldXZ)
{
    float hN = SampleHeightNormalized(worldXZ);
    return g_GrassGenCB.YOffset + (g_GrassGenCB.HeightOffset + hN * g_GrassGenCB.HeightScale);
}

// ------------------------------------------------------------
// Density (WORLD TILED) + contrast remap
// ------------------------------------------------------------
float RemapDensity(float d, float contrast01)
{
    float a = saturate(contrast01);
    a = min(a, 0.49f);
    return smoothstep(a, 1.0f - a, d);
}

float SampleWorldDensity_Tiled(float2 worldXZ)
{
    // Use CB tuning (with safe fallbacks)
    float tiling = (g_GrassGenCB.DensityTiling > 0.0f) ? g_GrassGenCB.DensityTiling : 0.002f;
    float contrast = (g_GrassGenCB.DensityContrast > 0.0f) ? g_GrassGenCB.DensityContrast : 0.25f;
    float powK = (g_GrassGenCB.DensityPow > 0.0f) ? g_GrassGenCB.DensityPow : 0.65f;

    float2 uv = worldXZ * tiling; // WRAP sampler -> tiled
    float d = g_DensityField.SampleLevel(g_LinearWrapSampler, uv, 0.0).r;
    d = saturate(d);
    d = RemapDensity(d, contrast);

    // pow < 1 boosts mid-values => more visible patches
    d = pow(d, powK);
    return d;
}

// ------------------------------------------------------------
// Slope/Height masks (simple + tunable)
// ------------------------------------------------------------
float ComputeSlope01(float2 worldXZ)
{
    float2 e = float2(g_GrassGenCB.SpacingX, g_GrassGenCB.SpacingZ);

    float hX1 = SampleHeightNormalized(worldXZ + float2(e.x, 0));
    float hX0 = SampleHeightNormalized(worldXZ - float2(e.x, 0));
    float hZ1 = SampleHeightNormalized(worldXZ + float2(0, e.y));
    float hZ0 = SampleHeightNormalized(worldXZ - float2(0, e.y));

    // Convert normalized height diff to world units diff
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

    // If user left defaults as 0/0, treat as "allow all".
    // (Optional safety: some people may forget to set these)
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

// ------------------------------------------------------------
// Frustum culling (AABB vs 6 planes)
// ------------------------------------------------------------
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

// ------------------------------------------------------------
// Chunk-based generation
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void GenerateGrassInstances(uint3 tid : SV_DispatchThreadID)
{
    int halfExt = g_GrassGenCB.ChunkHalfExtent;
    int2 chunkGrid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);

    float2 camXZ = float2(g_FrameCB.CameraPosition.x,
                          g_FrameCB.CameraPosition.z);

    float chunkSize = g_GrassGenCB.ChunkSize;

    int2 camChunk = int2(floor(camXZ / max(chunkSize, 1e-6)));
    int2 worldChunk = camChunk + chunkGrid;

    float2 chunkOriginXZ = float2(worldChunk) * chunkSize;

    // ------------------------------------------------------------
    // Chunk frustum culling (early out)
    // Conservative AABB height range (+/- 20m). You can tighten later.
    // ------------------------------------------------------------
    float chunkOriginHeight = SampleWorldHeight(chunkOriginXZ);
    float3 chunkMin = float3(chunkOriginXZ.x, chunkOriginHeight - 20.0, chunkOriginXZ.y);
    float3 chunkMax = float3(chunkOriginXZ.x + chunkSize, chunkOriginHeight + 20.0, chunkOriginXZ.y + chunkSize);

    if (!AabbInsideFrustum(chunkMin, chunkMax))
        return;

    uint chunkSeed = hash2i(worldChunk, g_GrassGenCB.SeedSalt);

    [loop]
    for (uint s = 0; s < g_GrassGenCB.SamplesPerChunk; ++s)
    {
        uint seed = wang_hash(chunkSeed ^ (s * 0x9E3779B9u));

        // ------------------------------------------------------------
        // Random point inside chunk (world XZ) with jitter
        // ------------------------------------------------------------
        float ux = rand01(seed ^ 0x2222u);
        float uz = rand01(seed ^ 0x3333u);

        float jx = (ux - 0.5f) * g_GrassGenCB.Jitter;
        float jz = (uz - 0.5f) * g_GrassGenCB.Jitter;

        float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * chunkSize;
        float2 posXZ = chunkOriginXZ + localXZ;

        // ------------------------------------------------------------
        // Radius culling (camera-centered window)
        // ------------------------------------------------------------
        float2 dc = posXZ - camXZ;
        if (dot(dc, dc) > g_GrassGenCB.SpawnRadius * g_GrassGenCB.SpawnRadius)
            continue;

        // ------------------------------------------------------------
        // Density base (WORLD-TILED)
        // ------------------------------------------------------------
        float density = SampleWorldDensity_Tiled(posXZ);
        if (density <= 0.001f)
            continue;

        // ------------------------------------------------------------
        // Slope/height masks
        // ------------------------------------------------------------
        float hN = SampleHeightNormalized(posXZ); // 0..1
        float slope01 = ComputeSlope01(posXZ); // 0..1-ish

        float slopeMask = 1.0f - slope01; // steep => less grass
        float heightMask = ComputeHeightMask(hN);

        density *= saturate(slopeMask) * heightMask;
        if (density <= 0.001f)
            continue;

        // ------------------------------------------------------------
        // IMPORTANT: prevent "averaging out" when SamplesPerChunk is large.
        // Gate attempts by density first.
        // ------------------------------------------------------------
        if (rand01(seed ^ 0x41A7u) > density)
            continue;

        // ------------------------------------------------------------
        // Spawn probability (fine control)
        // ------------------------------------------------------------
        float effectiveProb = saturate(g_GrassGenCB.SpawnProb * density);

        // Separate random tap to reduce correlation with jitter
        if (rand01(seed ^ 0x4444u) > effectiveProb)
            continue;

        // ------------------------------------------------------------
        // Allocate instance slot (atomic)
        // ------------------------------------------------------------
        uint idx;
        g_Counter.InterlockedAdd(0, 1, idx);

        if (idx >= kMaxInstances)
            return;

        // ------------------------------------------------------------
        // Height (world)
        // ------------------------------------------------------------
        float y = SampleWorldHeight(posXZ);

        GrassInstance inst;
        inst.PosWS = float3(posXZ.x, y, posXZ.y);

        // ------------------------------------------------------------
        // Scale
        // - Optional bias: lower density => slightly larger blades
        // ------------------------------------------------------------
        float scaleT = rand01(seed ^ 0x5555u);
        float densityScaleBias = lerp(1.2f, 0.8f, density); // low density -> bigger

        // NOTE: keep *0.04f if your grass mesh is huge.
        inst.Scale = lerp(g_GrassGenCB.MinScale, g_GrassGenCB.MaxScale, scaleT) * 0.04f * densityScaleBias;

        // Orientation
        inst.Yaw = rand01(seed ^ 0x6666u) * 6.2831853f;

        // Small initial lean (radians)
        inst.Pitch = lerp(-0.90f, 0.90f, rand01(seed ^ 0x7777u));

        // Bend stiffness
        inst.BendStrength =
            lerp(g_GrassGenCB.BendStrengthMin, g_GrassGenCB.BendStrengthMax, rand01(seed ^ 0x8888u));

        inst._pad0 = 0;

        g_OutInstances[idx] = inst;
    }
}

// ------------------------------------------------------------
// Indirect args
// ------------------------------------------------------------
static const uint kIndexCountPerInstance = 39;
static const uint kStartIndexLocation = 0;
static const uint kBaseVertexLocation = 0;
static const uint kStartInstanceLocation = 0;

[numthreads(1, 1, 1)]
void WriteIndirectArgs(uint3 tid : SV_DispatchThreadID)
{
    uint instanceCount = g_Counter.Load(0);
    instanceCount = min(instanceCount, kMaxInstances);

    g_IndirectArgs.Store(0, kIndexCountPerInstance);
    g_IndirectArgs.Store(4, instanceCount);
    g_IndirectArgs.Store(8, kStartIndexLocation);
    g_IndirectArgs.Store(12, kBaseVertexLocation);
    g_IndirectArgs.Store(16, kStartInstanceLocation);
}
