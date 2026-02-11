// Shaders/GrassChunkPoolCS.hlsl
//
// Strategy (your choice #1):
// - FillNewPoolsCS: ALWAYS fill full SamplesPerChunk (max) for newly allocated pools.
//                  (Spawn cost happens only when pool is allocated / becomes dirty.)
// - BuildInstancesFromPoolsCS: EVERY FRAME decide how many samples to USE per chunk
//                              via distance-based ComputeSamplesPerChunk + densityScaled,
//                              then only iterate s < samplesThisChunk.
// - UpdateChunkPoolsCS: alloc/release + dirty mark (unchanged), BUT we also store chunk center height
//                       for frustum culling in Build (cheap: 1 height sample per chunk in Fill).
//
// Added:
// - View frustum culling per chunk (AABB vs 6 planes) in BuildInstancesFromPoolsCS using cached chunk height.
// - Distance-based density throttling (samplesThisChunk) in BuildInstancesFromPoolsCS (per chunk).
//
// Entry points:
//  - UpdateChunkPoolsCS
//  - FillNewPoolsCS
//  - BuildInstancesFromPoolsCS
//
// Assumptions:
//  - IndirectCountBuffer layout: uint counter per slot at byteOffset = slot * 4
//  - VisibleDim = 2*ChunkHalfExtent (typically 128)
//  - NumPools = VisibleDim*VisibleDim
//
// NOTE:
// - This file expects FrameConstants (g_FrameCB) contains CameraPosition and FrustumPlanesWS[6].
// - g_TerrainNormal is not used here.
// - PoolPositions stores float4(x, y, z, press01) for valid samples; invalid sentinel uses NaN y.
// - PoolChunkCoord buffer stores {uint pad0; int2 chunkCoord; float chunkHeight; uint pad1} (16B stride).

#include "Common.hlsli"
#include "HeightField.hlsli"
#include "GrassCommon.hlsli"

// ---------------------------------------------------------------------------
// Constant buffers
// ---------------------------------------------------------------------------

cbuffer HEIGHT_FIELD_CONSTANTS
{
    HeightFieldConstants g_HeightFieldCB;
};

cbuffer GRASS_GEN_CONSTANTS
{
    GrassGenConstants g_CB;
};

// ---------------------------------------------------------------------------
// Structured layouts (16-byte stride everywhere)
// ---------------------------------------------------------------------------

// Visible cell table: 16 bytes
// uint PoolIndex; int2 ChunkCoord; uint _pad
struct VisibleCell
{
    uint PoolIndex;
    int2 ChunkCoord;
    uint _pad;
};

// PoolChunkCoord: 16 bytes
// uint _pad0; int2 ChunkCoord; float ChunkHeight;
struct PoolChunkCoord
{
    uint _pad0;
    int2 ChunkCoord;
    float ChunkHeight;
};

// PoolDirty: 16 bytes
// uint Dirty; uint3 _pad
struct PoolDirty
{
    uint Dirty;
    uint3 _pad;
};

// FreeList: 16 bytes
// uint Value; uint3 _pad
struct FreeListItem
{
    uint Value;
    uint3 _pad;
};

// ---------------------------------------------------------------------------
// UAVs / SRVs
// ---------------------------------------------------------------------------

RWStructuredBuffer<VisibleCell> g_VisibleCellTable;
RWStructuredBuffer<PoolChunkCoord> g_PoolChunkCoord;
RWStructuredBuffer<PoolDirty> g_PoolDirty;

RWStructuredBuffer<FreeListItem> g_FreeList;
RWByteAddressBuffer g_FreeListCounter; // uint at byte 0

RWStructuredBuffer<float4> g_PoolPositions; // [pool * SamplesPerChunk + i] = float4(x,y,z,press01)

// Rendering outputs (existing)
RWStructuredBuffer<GrassMeshInstance> g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance> g_OutInstancesLOD2;

// slot counters (IndirectCountBuffer)
RWByteAddressBuffer g_CounterBuffer;

// Inputs for spawn (FillNewPools only)
Texture2D<float> g_HeightField;
Texture2D<float> g_DensityField;
Texture2D<float> g_InteractionField;

// ---------------------------------------------------------------------------
// Constants / helpers
// ---------------------------------------------------------------------------

static const uint INVALID_U32 = 0xFFFFFFFFu;
static const float INVALID_NAN = asfloat(0x7FC00000u);

// ---------------------------------------------------------------------------
// Random / Hash
// ---------------------------------------------------------------------------

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
    return (WangHash(seed) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

uint Hash2i(int2 v, uint salt)
{
    uint x = (uint) v.x;
    uint y = (uint) v.y;
    return (x * 73856093u) ^ (y * 19349663u) ^ salt;
}

// ---------------------------------------------------------------------------
// Chunk mapping
// ---------------------------------------------------------------------------

int2 WorldXZToChunkCoord(float2 worldXZ, float chunkSize)
{
    float2 rel = worldXZ - g_HeightFieldCB.WorldOriginXZ;
    float inv = rcp(max(chunkSize, 1e-6f));
    float2 c = floor(rel * inv);
    return int2((int) c.x, (int) c.y);
}

float2 ChunkCoordToWorldOrigin(int2 chunkCoord, float chunkSize)
{
    return g_HeightFieldCB.WorldOriginXZ + float2(chunkCoord) * chunkSize;
}

bool ClampChunkToHeightfield(inout float2 chunkOriginXZ, float chunkSize)
{
    float2 hfMin = g_HeightFieldCB.WorldOriginXZ;
    float2 hfMax = g_HeightFieldCB.WorldOriginXZ + g_HeightFieldCB.WorldSizeXZ;

    float2 o = chunkOriginXZ;
    float2 e = o + chunkSize.xx;

    if (e.x < hfMin.x || e.y < hfMin.y || o.x > hfMax.x || o.y > hfMax.y)
        return false;

    chunkOriginXZ = clamp(chunkOriginXZ, hfMin - chunkSize.xx, hfMax);
    return true;
}

// ---------------------------------------------------------------------------
// Height / Density / Interaction sampling
// ---------------------------------------------------------------------------

float2 WorldXZToHeightUV(float2 worldXZ)
{
    return HF_WorldXZToUV(worldXZ, g_HeightFieldCB, float2(1.0f, 1.0f), float2(0.0f, 0.0f));
}

float SampleHeightNormalized(float2 worldXZ)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    return HF_SampleHeight01(g_HeightField, g_LinearClampSampler, uv);
}

float SampleWorldHeight(float2 worldXZ)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    return HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uv, g_HeightFieldCB, g_CB.YOffset);
}

float RemapDensity(float d, float contrast01)
{
    float a = saturate(contrast01);
    a = min(a, 0.49f);
    return smoothstep(a, 1.0f - a, d);
}

float SampleWorldDensity(float2 worldXZ, float mipLevel)
{
    float tiling = (g_CB.DensityTiling > 0.0f) ? g_CB.DensityTiling : 0.002f;
    float contrast = (g_CB.DensityContrast > 0.0f) ? g_CB.DensityContrast : 0.25f;
    float powK = (g_CB.DensityPow > 0.0f) ? g_CB.DensityPow : 0.65f;

    float2 uv = worldXZ * tiling;
    float d = g_DensityField.SampleLevel(g_LinearWrapSampler, uv, mipLevel).r;

    d = saturate(d);
    d = RemapDensity(d, contrast);
    d = pow(d, powK);

    return d;
}

float SampleInteraction(float2 worldXZ)
{
    float2 uvLocal = (worldXZ - g_CB.InteractionOriginXZ) * g_CB.InteractionInvWorldSizeXZ;
    float2 uvRingOffset = float2(g_CB.InteractionTexelOrigin) * g_CB.InteractionInvFieldSize;
    float2 uv = frac(uvLocal + uvRingOffset);
    return g_InteractionField.SampleLevel(g_LinearWrapSampler, uv, 0.0f).r;
}

// Height band-pass mask
float ComputeHeightMask(float hN)
{
    float hMinN = g_CB.HeightMinN;
    float hMaxN = g_CB.HeightMaxN;
    float hFadeN = max(g_CB.HeightFadeN, 1e-6f);

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

// ---------------------------------------------------------------------------
// Frustum culling (AABB vs 6 planes)
// ---------------------------------------------------------------------------

bool AabbInsideFrustum(float3 bmin, float3 bmax)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float4 P = g_FrameCB.FrustumPlanesWS[i];
        float3 n = P.xyz;
        float d = P.w;

        float3 v;
        v.x = (n.x >= 0.0f) ? bmax.x : bmin.x;
        v.y = (n.y >= 0.0f) ? bmax.y : bmin.y;
        v.z = (n.z >= 0.0f) ? bmax.z : bmin.z;

        if (dot(n, v) + d < 0.0f)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Distance-based sample scaling (used in Build)
// ---------------------------------------------------------------------------

// Coarse density mip for per-chunk cache.
static const float CHUNK_DENSITY_MIP = 3.0f;

// If cached density is below this, skip the chunk early.
static const float DENSITY_DISABLE_THRESHOLD = 0.01f;

// Minimum samples for far chunks.
static const uint SAMPLES_MIN_FAR = 64u;

// Safety clamp.
static const uint SAMPLES_MAX_CLAMP = 4096u;

float ComputeDistanceSampleScale(float distSqr, float lod0Sqr, float spawnRadiusSqr)
{
    float t = (distSqr - lod0Sqr) / max(spawnRadiusSqr - lod0Sqr, 1e-6f);
    t = saturate(t);
    t = t * t;
    return 1.0f - t;
}

uint ComputeSamplesPerChunk(uint baseSamples, float distSqr, float lod0Sqr, float spawnRadiusSqr)
{
    float s = ComputeDistanceSampleScale(distSqr, lod0Sqr, spawnRadiusSqr);
    uint target = (uint) round((float) baseSamples * s);

    uint minS = SAMPLES_MIN_FAR;
    target = max(target, minS);
    target = min(target, min(baseSamples, SAMPLES_MAX_CLAMP));
    return target;
}

// ---------------------------------------------------------------------------
// Free-list stack ops (counter in byteaddress at offset 0)
// ---------------------------------------------------------------------------

uint FreeList_Pop()
{
    uint old = 0u;
    g_FreeListCounter.InterlockedAdd(0, -1, old);

    if (old == 0u)
    {
        g_FreeListCounter.InterlockedAdd(0, +1, old);
        return INVALID_U32;
    }

    uint idx = old - 1u;
    return g_FreeList[idx].Value;
}

void FreeList_Push(uint value)
{
    uint old = 0u;
    g_FreeListCounter.InterlockedAdd(0, +1, old);
    g_FreeList[old].Value = value;
}

// ---------------------------------------------------------------------------
// Wave helpers (for counter reserve/scatter)
// ---------------------------------------------------------------------------

struct BallotMask
{
    uint4 M;
};

BallotMask WaveBallotWrap(bool pred)
{
    BallotMask b;
    b.M = WaveActiveBallot(pred);
    return b;
}

uint BallotCountBits(BallotMask b)
{
    return countbits(b.M.x) + countbits(b.M.y) + countbits(b.M.z) + countbits(b.M.w);
}

uint BallotPrefixCountBits(BallotMask b, uint lane)
{
    uint word = lane >> 5;
    uint bit = lane & 31;
    uint prefix = 0u;

    if (word > 0u)
        prefix += countbits(b.M.x);
    if (word > 1u)
        prefix += countbits(b.M.y);
    if (word > 2u)
        prefix += countbits(b.M.z);

    uint w =
        (word == 0u) ? b.M.x :
        (word == 1u) ? b.M.y :
        (word == 2u) ? b.M.z : b.M.w;

    uint mask = (bit == 0u) ? 0u : ((1u << bit) - 1u);
    prefix += countbits(w & mask);

    return prefix;
}

uint WaveReserveByteAddressCounter(uint counterByteOffset, BallotMask ballot)
{
    uint base = 0u;
    uint cnt = BallotCountBits(ballot);

    if (cnt != 0u && WaveIsFirstLane())
    {
        g_CounterBuffer.InterlockedAdd(counterByteOffset, cnt, base);
    }
    return WaveReadLaneFirst(base);
}

// ---------------------------------------------------------------------------
// Instance packing helpers
// ---------------------------------------------------------------------------

uint MakeSeed8(uint seed)
{
    return (WangHash(seed) >> 24) & 0xFFu;
}
uint MakeVariantId(uint seed)
{
    return (WangHash(seed ^ 0xBEEFu) >> 30) & 0x3u;
}
uint MakeAtlasIndex(uint seed)
{
    return (WangHash(seed ^ 0xCAFEu) >> 29) & 0x7u;
}

// ---------------------------------------------------------------------------
// Entry A) UpdateChunkPoolsCS
// ---------------------------------------------------------------------------
// One thread per visible cell.
// Compares desired chunkCoord vs current; if mismatch, release old pool and alloc new,
// mark newly allocated pool dirty.
[numthreads(8, 8, 1)]
void UpdateChunkPoolsCS(uint3 tid : SV_DispatchThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    if (tid.x >= dim || tid.y >= dim)
        return;

    uint cellIndex = tid.y * dim + tid.x;

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
    int2 camChunk = WorldXZToChunkCoord(camXZ, g_CB.ChunkSize);

    int halfExt = (int) g_CB.ChunkHalfExtent;
    int2 grid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);
    int2 desiredChunk = camChunk + grid;

    VisibleCell cell = g_VisibleCellTable[cellIndex];

    bool same = all(cell.ChunkCoord == desiredChunk) && (cell.PoolIndex != INVALID_U32);
    if (same)
        return;

    // release old pool
    if (cell.PoolIndex != INVALID_U32)
    {
        uint oldPool = cell.PoolIndex;

        PoolChunkCoord pc = g_PoolChunkCoord[oldPool];
        pc.ChunkCoord = int2(0x80000000, 0x80000000);
        pc.ChunkHeight = 0.0f;
        g_PoolChunkCoord[oldPool] = pc;

        PoolDirty pd = g_PoolDirty[oldPool];
        pd.Dirty = 0u;
        g_PoolDirty[oldPool] = pd;

        FreeList_Push(oldPool);
    }

    // alloc new pool
    uint newPool = FreeList_Pop();
    if (newPool == INVALID_U32)
    {
        cell.PoolIndex = INVALID_U32;
        cell.ChunkCoord = desiredChunk;
        g_VisibleCellTable[cellIndex] = cell;
        return;
    }

    // assign mapping
    cell.PoolIndex = newPool;
    cell.ChunkCoord = desiredChunk;
    g_VisibleCellTable[cellIndex] = cell;

    // update pool chunk coord
    PoolChunkCoord pc2 = g_PoolChunkCoord[newPool];
    pc2.ChunkCoord = desiredChunk;
    pc2.ChunkHeight = 0.0f; // will be set in FillNewPoolsCS
    g_PoolChunkCoord[newPool] = pc2;

    // mark dirty -> FillNewPoolsCS will populate pool positions
    PoolDirty pd2 = g_PoolDirty[newPool];
    pd2.Dirty = 1u;
    g_PoolDirty[newPool] = pd2;
}

// ---------------------------------------------------------------------------
// Entry B) FillNewPoolsCS
// ---------------------------------------------------------------------------
// One thread-group per visible cell (gid.x == cellIndex).
// Only runs for dirty pools.
// ALWAYS fills full SamplesPerChunk (max). (No distance-based sample count here.)
//
// Also caches ChunkHeight into g_PoolChunkCoord[pool].ChunkHeight for frustum AABB test in Build.
[numthreads(256, 1, 1)]
void FillNewPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    uint visibleCells = dim * dim;

    uint cellIndex = gid.x;
    if (cellIndex >= visibleCells)
        return;

    VisibleCell cell = g_VisibleCellTable[cellIndex];
    uint pool = cell.PoolIndex;
    if (pool == INVALID_U32)
        return;

    PoolDirty pd = g_PoolDirty[pool];
    if (pd.Dirty == 0u)
        return;

    int2 chunkCoord = cell.ChunkCoord;
    float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);

    if (!ClampChunkToHeightfield(chunkOriginXZ, g_CB.ChunkSize))
    {
        // mark everything invalid, clear dirty
        uint base = pool * g_CB.SamplesPerChunk;
        for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
        {
            g_PoolPositions[base + s] = float4(0.0f, INVALID_NAN, 0.0f, 0.0f);
        }
        GroupMemoryBarrierWithGroupSync();
        if (tid.x == 0u)
        {
            pd.Dirty = 0u;
            g_PoolDirty[pool] = pd;
        }
        return;
    }

    // Cache chunk height once (chunk origin)
    // Note: this is 1 tap, amortized (only when pool becomes dirty).
    float chunkHeight = SampleWorldHeight(chunkOriginXZ);

    if (tid.x == 0u)
    {
        PoolChunkCoord pc = g_PoolChunkCoord[pool];
        pc.ChunkCoord = chunkCoord;
        pc.ChunkHeight = chunkHeight;
        g_PoolChunkCoord[pool] = pc;
    }

    // Per-chunk coarse density cache (for thinning when generating positions)
    float2 chunkCenterXZ = chunkOriginXZ + 0.5f * g_CB.ChunkSize.xx;
    float chunkDensity = SampleWorldDensity(chunkCenterXZ, CHUNK_DENSITY_MIP);

    uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);
    uint base = pool * g_CB.SamplesPerChunk;

    // Always fill full SamplesPerChunk, but still do height/density thinning per sample.
    for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
    {
        uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

        float ux = Rand01(seed ^ 0x2222u);
        float uz = Rand01(seed ^ 0x3333u);

        float jx = (ux - 0.5f) * g_CB.Jitter;
        float jz = (uz - 0.5f) * g_CB.Jitter;

        float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * g_CB.ChunkSize;
        float2 posXZ = chunkOriginXZ + localXZ;

        // Height band
        float hN = SampleHeightNormalized(posXZ);
        float heightMask = ComputeHeightMask(hN);

        float density = chunkDensity * heightMask;
        if (density <= 0.001f)
        {
            g_PoolPositions[base + s] = float4(posXZ.x, INVALID_NAN, posXZ.y, 0.0f);
            continue;
        }

        float press01 = saturate(SampleInteraction(posXZ));

        // Density-based thinning
        if (Rand01(seed ^ 0x41A7u) > density)
        {
            g_PoolPositions[base + s] = float4(posXZ.x, INVALID_NAN, posXZ.y, 0.0f);
            continue;
        }

        // SpawnProb gate
        float effectiveProb = saturate(g_CB.SpawnProb * density);
        if (Rand01(seed ^ 0x4444u) > effectiveProb)
        {
            g_PoolPositions[base + s] = float4(posXZ.x, INVALID_NAN, posXZ.y, 0.0f);
            continue;
        }

        float y = SampleWorldHeight(posXZ);
        g_PoolPositions[base + s] = float4(posXZ.x, y, posXZ.y, press01);
    }

    GroupMemoryBarrierWithGroupSync();
    if (tid.x == 0u)
    {
        pd.Dirty = 0u;
        g_PoolDirty[pool] = pd;
    }
}

// ---------------------------------------------------------------------------
// Entry C) BuildInstancesFromPoolsCS
// ---------------------------------------------------------------------------
// One thread-group per visible cell (pool).
// - Clears 3 counters once per dispatch (gid==0, tid==0).
// - Per pool: frustum cull by chunk AABB (cached chunk height) BEFORE doing any per-sample work.
// - Per pool: compute samplesThisChunk based on distance and chunkDensity (coarse), then only process that many samples.
// - No Height/Density/Interaction sampling per candidate here (only 1 density sample per chunk).
[numthreads(256, 1, 1)]
void BuildInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    uint visibleCells = dim * dim;

    // Clear counters once (single lane)
    if (gid.x == 0u && tid.x == 0u)
    {
        uint off0 = (g_CB.IndirectSlotLOD0 << 2);
        uint off1 = (g_CB.IndirectSlotLOD1 << 2);
        uint off2 = (g_CB.IndirectSlotLOD2 << 2);

        g_CounterBuffer.Store(off0, 0u);
        g_CounterBuffer.Store(off1, 0u);
        g_CounterBuffer.Store(off2, 0u);
    }

    GroupMemoryBarrierWithGroupSync();

    uint cellIndex = gid.x;
    if (cellIndex >= visibleCells)
        return;

    VisibleCell cell = g_VisibleCellTable[cellIndex];
    uint pool = cell.PoolIndex;
    if (pool == INVALID_U32)
        return;

    // Chunk origin + AABB for frustum culling
    int2 chunkCoord = cell.ChunkCoord;
    float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);

    // Validate within heightfield bounds (should usually be true)
    float2 chunkOriginClamped = chunkOriginXZ;
    if (!ClampChunkToHeightfield(chunkOriginClamped, g_CB.ChunkSize))
        return;

    // Cached chunk height (from Fill), fallback if not set
    PoolChunkCoord pc = g_PoolChunkCoord[pool];
    float chunkHeight = pc.ChunkHeight;
    // If not initialized, sample once (rare).
    if (chunkHeight == 0.0f)
        chunkHeight = SampleWorldHeight(chunkOriginClamped);

    // AABB thickness guess (tune)
    float3 chunkMin = float3(chunkOriginClamped.x, chunkHeight - 20.0f, chunkOriginClamped.y);
    float3 chunkMax = float3(chunkOriginClamped.x + g_CB.ChunkSize, chunkHeight + 20.0f, chunkOriginClamped.y + g_CB.ChunkSize);

    if (!AabbInsideFrustum(chunkMin, chunkMax))
        return;

    // Distance-based sample usage per chunk (per frame)
    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
    float2 chunkCenterXZ = chunkOriginClamped + 0.5f * g_CB.ChunkSize.xx;

    float2 dChunk = chunkCenterXZ - camXZ;
    float distChunkSqr = dot(dChunk, dChunk);

    float spawnRadiusSqr = g_CB.SpawnRadius * g_CB.SpawnRadius;
    if (distChunkSqr > spawnRadiusSqr)
        return;

    float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
    float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

    // Coarse density cache per chunk (1 sample per chunk per frame)
    float chunkDensity = SampleWorldDensity(chunkCenterXZ, CHUNK_DENSITY_MIP);
    if (chunkDensity <= DENSITY_DISABLE_THRESHOLD)
        return;

    uint baseSamples = g_CB.SamplesPerChunk;
    uint samplesThisChunk = ComputeSamplesPerChunk(baseSamples, distChunkSqr, lod0Sqr, spawnRadiusSqr);

    // Density-scaled usage
    uint densityScaled = (uint) round((float) samplesThisChunk * saturate(chunkDensity));
    samplesThisChunk = max(densityScaled, (uint) min(SAMPLES_MIN_FAR, samplesThisChunk));

    // Clamp to pool capacity
    samplesThisChunk = min(samplesThisChunk, baseSamples);
    if (samplesThisChunk == 0u)
        return;

    // Prepare counter offsets
    uint off0 = (g_CB.IndirectSlotLOD0 << 2);
    uint off1 = (g_CB.IndirectSlotLOD1 << 2);
    uint off2 = (g_CB.IndirectSlotLOD2 << 2);

    uint base = pool * g_CB.SamplesPerChunk;

    // Per-instance params are deterministic from (chunkCoord, s) so stable even if samplesThisChunk changes.
    uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);

    for (uint s = tid.x; s < samplesThisChunk; s += 256u)
    {
        float4 p = g_PoolPositions[base + s];

        if (isnan(p.y))
            continue;

        float3 posWS = float3(p.x, p.y, p.z);
        float press01 = saturate(p.w);

        float2 dxz = float2(posWS.x, posWS.z) - camXZ;
        float distSqr = dot(dxz, dxz);

        // Deterministic instance randoms (match Fill seeds)
        uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

        float scaleT = Rand01(seed ^ 0x5555u);
        float scale =
            lerp(g_CB.MinScale, g_CB.MaxScale, scaleT) *
            0.04f;

        float yaw = Rand01(seed ^ 0x6666u) * GRASS_TWO_PI;
        float pitch = lerp(g_CB.MinPitch, g_CB.MaxPitch, Rand01(seed ^ 0x7777u));
        float bend01 = lerp(g_CB.BendStrengthMin, g_CB.BendStrengthMax, Rand01(seed ^ 0x8888u));

        uint seed8 = MakeSeed8(seed ^ 0x1234u);
        uint variantId = MakeVariantId(seed);
        uint atlasIndex = MakeAtlasIndex(seed);

        bool emit0 = (distSqr < lod0Sqr);
        bool emit1 = (!emit0) && (distSqr < lod1Sqr);
        bool emit2 = (!emit0) && (!emit1);

        BallotMask b0 = WaveBallotWrap(emit0);
        BallotMask b1 = WaveBallotWrap(emit1);
        BallotMask b2 = WaveBallotWrap(emit2);

        uint base0 = WaveReserveByteAddressCounter(off0, b0);
        uint base1 = WaveReserveByteAddressCounter(off1, b1);
        uint base2 = WaveReserveByteAddressCounter(off2, b2);

        uint lane = WaveGetLaneIndex();

        if (emit0)
        {
            uint pfx = BallotPrefixCountBits(b0, lane);

            GrassMeshInstance inst = MakeGrassMeshInstance(
                posWS, scale, yaw, pitch, bend01, press01, variantId, seed8);

            g_OutInstancesLOD0[base0 + pfx] = inst;
        }
        else if (emit1)
        {
            uint pfx = BallotPrefixCountBits(b1, lane);

            GrassCrossPlaneInstance inst = MakeGrassCrossPlaneInstance(
                posWS, scale, yaw, bend01, press01, variantId, seed8, 0u, 0u);

            g_OutInstancesLOD1[base1 + pfx] = inst;
        }
        else // emit2
        {
            uint pfx = BallotPrefixCountBits(b2, lane);

            GrassBillboardInstance inst = MakeGrassBillboardInstance(
                posWS, scale, yaw, atlasIndex, seed8);

            g_OutInstancesLOD2[base2 + pfx] = inst;
        }
    }
}
