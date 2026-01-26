#include "HLSL_Structures.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

RWStructuredBuffer<GrassInstance> g_OutInstances;

// 20 bytes: D3D12_DRAW_INDEXED_ARGUMENTS layout
RWByteAddressBuffer g_IndirectArgs;

// uint Counter (4 bytes) at byte offset 0
RWByteAddressBuffer g_Counter;

static const uint kMaxInstances = 1u << 20; // 1,048,576

// Heightmap (UNORM16)
Texture2D<float> g_HeightMap;
SamplerState g_LinearWrapSampler;

static const float kHeightScale = 1000.0;
static const float kHeightOffset = -10.0;

static const float2 kTerrainOriginXZ = float2(0.0, 0.0); // heightfield (0,0) at world
static const float2 kTerrainSizeXZ = float2(4096.0, 4096.0); // world meters covered by heightmap (X,Z)

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
float SampleWorldHeight(float2 worldXZ)
{
    // World -> UV
    float2 uv = (worldXZ - kTerrainOriginXZ) / max(kTerrainSizeXZ, float2(1e-6, 1e-6));
    uv = saturate(uv);

    // Sample normalized (0..1)
    float hN = g_HeightMap.SampleLevel(g_LinearWrapSampler, uv, 0.0);

    // Decode to world height
    return kHeightOffset + hN * kHeightScale;
}

// ------------------------------------------------------------
// Chunk config
// ------------------------------------------------------------
static const float kChunkSize = 4.0f; // 4m x 4m
static const int kChunkHalfExtent = 32; // (2*32)^2 = 4096 chunks around camera

// Samples per chunk (trade quality/perf)
static const uint kSamplesPerChunk = 256; // e.g. 16 points per 4x4m => density 조절

// Jitter inside chunk (0..1 range)
static const float kJitter = 0.9f;

// Placement + appearance
static const float kMinScale = 0.8f;
static const float kMaxScale = 1.2f;

// Density probability per sample (0..1). 0.05 = 5%
static const float kSpawnProb = 0.05f;

// Optional: radius culling in meters (camera-centered draw window)
static const float kSpawnRadius = 1000.0f;

// ------------------------------------------------------------
// Hash / random
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
// Chunk-based generation
// Each thread == one chunk
// Dispatch size: (2*kChunkHalfExtent) x (2*kChunkHalfExtent)
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void GenerateGrassInstances(uint3 tid : SV_DispatchThreadID)
{
    int2 chunkGrid = int2((int) tid.x - kChunkHalfExtent, (int) tid.y - kChunkHalfExtent);

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);

    int2 camChunk = int2(floor(camXZ / kChunkSize));
    int2 worldChunk = camChunk + chunkGrid;

    float2 chunkOriginXZ = float2(worldChunk) * kChunkSize;

    const uint chunkSeed = hash2i(worldChunk, 0xA53A9E37u);

    [loop]
    for (uint s = 0; s < kSamplesPerChunk; ++s)
    {
        uint seed = wang_hash(chunkSeed ^ (s * 0x9E3779B9u));

        if (rand01(seed ^ 0x7777u) > kSpawnProb)
            continue;

        float ux = rand01(seed ^ 0x1234u);
        float uz = rand01(seed ^ 0xBEEFu);

        float jx = (ux - 0.5f) * kJitter;
        float jz = (uz - 0.5f) * kJitter;

        float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * kChunkSize;
        float2 posXZ = chunkOriginXZ + localXZ;

        float2 d = posXZ - camXZ;
        float dist2 = dot(d, d);

        if (dist2 > kSpawnRadius * kSpawnRadius)
            continue;

        float keepProb = 0.95;
        if (rand01(seed ^ 0x1357u) > keepProb)
            continue;

        uint idx;
        g_Counter.InterlockedAdd(0, 1, idx);

        if (idx >= kMaxInstances)
            return;

        // --- HeightMap 적용 ---
        float y = SampleWorldHeight(posXZ);

        GrassInstance inst = (GrassInstance) 0;
        inst.PosWS = float3(posXZ.x, y, posXZ.y);
        inst.Yaw = rand01(seed ^ 0x9999u) * 6.2831853f;
        inst.Scale = lerp(kMinScale, kMaxScale, rand01(seed ^ 0x2222u)) * 0.04;

        g_OutInstances[idx] = inst;
    }
}

// ------------------------------------------------------------
// Indirect args
// ------------------------------------------------------------
static const uint kIndexCountPerInstance = 13344;
static const uint kStartIndexLocation = 0;
static const uint kBaseVertexLocation = 0;
static const uint kStartInstanceLocation = 0;

[numthreads(1, 1, 1)]
void WriteIndirectArgs(uint3 tid : SV_DispatchThreadID)
{
    uint instanceCount = g_Counter.Load(0);

    g_IndirectArgs.Store(0, kIndexCountPerInstance);
    g_IndirectArgs.Store(4, instanceCount);
    g_IndirectArgs.Store(8, kStartIndexLocation);
    g_IndirectArgs.Store(12, kBaseVertexLocation);
    g_IndirectArgs.Store(16, kStartInstanceLocation);
}