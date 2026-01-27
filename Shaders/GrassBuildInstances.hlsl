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

static const uint kMaxInstances = 1u << 24; // 1,048,576

// Heightmap (R16_UNORM sampled as normalized float 0..1)
Texture2D<float> g_HeightMap;
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

float2 normalizeSafe(float2 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-8)
        return float2(1, 0);
    return v * rsqrt(len2);
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

float SampleWorldHeight(float2 worldXZ)
{
    float2 uv;
    uv.x = WorldXZToU(worldXZ.x);
    uv.y = WorldXZToV(worldXZ.y);
    uv = saturate(uv);

    float hN = g_HeightMap.SampleLevel(g_LinearWrapSampler, uv, 0.0);
    return g_GrassGenCB.YOffset + (g_GrassGenCB.HeightOffset + hN * g_GrassGenCB.HeightScale);
}

bool AabbInsideFrustum(float3 bmin, float3 bmax)
{
    // Conservative: if AABB is completely outside any plane -> cull
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float4 P = g_FrameCB.FrustumPlanesWS[i];
        float3 n = P.xyz;
        float d = P.w;

        // Select the vertex most in direction of plane normal (positive vertex)
        float3 v;
        v.x = (n.x >= 0.0) ? bmax.x : bmin.x;
        v.y = (n.y >= 0.0) ? bmax.y : bmin.y;
        v.z = (n.z >= 0.0) ? bmax.z : bmin.z;

        if (dot(n, v) + d < 0.0)
            return false; // fully outside
    }
    return true;
}

// ------------------------------------------------------------
// Chunk-based generation
// Each thread == one chunk
// Dispatch size: (2*HalfExtent) x (2*HalfExtent)
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
    // Chunk frustum culling (do this before per-sample work)
    // Chunk AABB in world space
    // ------------------------------------------------------------
    float chunkOriginHeight = SampleWorldHeight(chunkOriginXZ);
    float3 chunkMin = float3(chunkOriginXZ.x, chunkOriginHeight - 20.0, chunkOriginXZ.y);
    float3 chunkMax = float3(chunkOriginXZ.x + chunkSize, chunkOriginHeight + 20.0, chunkOriginXZ.y + chunkSize);

    // Cull whole chunk if outside frustum
    if (!AabbInsideFrustum(chunkMin, chunkMax))
    {
        return;
    }
    
    uint chunkSeed = hash2i(worldChunk, g_GrassGenCB.SeedSalt);

    [loop]
    for (uint s = 0; s < g_GrassGenCB.SamplesPerChunk; ++s)
    {
        uint seed = wang_hash(chunkSeed ^ (s * 0x9E3779B9u));

        // Spawn probability
        if (rand01(seed ^ 0x1111u) > g_GrassGenCB.SpawnProb)
        {
            continue;
        }

        // Random point inside chunk
        float ux = rand01(seed ^ 0x2222u);
        float uz = rand01(seed ^ 0x3333u);

        float jx = (ux - 0.5f) * g_GrassGenCB.Jitter;
        float jz = (uz - 0.5f) * g_GrassGenCB.Jitter;

        float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * chunkSize;
        float2 posXZ = chunkOriginXZ + localXZ;

        // Radius culling
        float2 d = posXZ - camXZ;
        if (dot(d, d) > g_GrassGenCB.SpawnRadius * g_GrassGenCB.SpawnRadius)
        {
            continue;
        }

        uint idx;
        g_Counter.InterlockedAdd(0, 1, idx);

        if (idx >= kMaxInstances)
        {
            return;
        }

        // Height
        float y = SampleWorldHeight(posXZ);

        GrassInstance inst;
        inst.PosWS = float3(posXZ.x, y, posXZ.y);

        // Scale
        inst.Scale =
            lerp(g_GrassGenCB.MinScale, g_GrassGenCB.MaxScale, rand01(seed ^ 0x5555u)) * 0.04f;

        // Orientation
        inst.Yaw = rand01(seed ^ 0x6666u) * 6.2831853f;

        // Small initial lean (¡¾ ~15 degrees)
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
