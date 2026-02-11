// Shaders/GrassChunkPoolCS.hlsl
//
// Entry points:
//  - UpdateChunkPoolsCS          : alloc/release + dirty mark (128x128)
//  - FillNewPoolsCS              : dirty pool만 spawn positions 생성
//  - BuildInstancesFromPoolsCS   : 매 프레임 positions -> LOD instance buffers + indirect counters
//
// Pool stores only positions (float3) -> stored as float4 (xyz, _)
//
// Assumptions:
//  - IndirectCountBuffer layout: uint counter per slot at byteOffset = slot * 4
//  - VisibleDim = 128 (2*HalfExtent)
//  - NumPools = VisibleDim*VisibleDim

#include "Common.hlsli"
#include "HeightField.hlsli"
#include "GrassCommon.hlsli"

cbuffer HEIGHT_FIELD_CONSTANTS
{
    HeightFieldConstants g_HeightFieldCB;
};

cbuffer GRASS_GEN_CONSTANTS
{
    GrassGenConstants g_CB;
};

// ---------------------------------------------------------------------------
// Visible cell table
// ---------------------------------------------------------------------------
// 16 bytes stride:
// uint PoolIndex; int2 ChunkCoord; uint _pad
struct VisibleCell
{
    uint PoolIndex;
    int2 ChunkCoord;
    uint _pad;
};

// PoolChunkCoord buffer (16 stride):
// uint _pad0; int2 ChunkCoord; uint _pad1
struct PoolChunkCoord
{
    uint _pad0;
    int2 ChunkCoord;
    uint _pad1;
};

// PoolDirty buffer (16 stride):
// uint Dirty; uint3 _pad
struct PoolDirty
{
    uint Dirty;
    uint3 _pad;
};

// FreeList buffer (16 stride):
// uint Value; uint3 _pad
struct FreeListItem
{
    uint Value;
    uint3 _pad;
};

// ---------------------------------------------------------------------------
// UAVs/SRVs
// ---------------------------------------------------------------------------
RWStructuredBuffer<VisibleCell> g_VisibleCellTable;
RWStructuredBuffer<PoolChunkCoord> g_PoolChunkCoord;
RWStructuredBuffer<PoolDirty> g_PoolDirty;

RWStructuredBuffer<FreeListItem> g_FreeList;
RWByteAddressBuffer g_FreeListCounter; // uint at byte 0

RWStructuredBuffer<float4> g_PoolPositions; // [pool * SamplesPerChunk + i] = float4(x,y,z,_)

// Rendering outputs (existing)
RWStructuredBuffer<GrassMeshInstance> g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance> g_OutInstancesLOD2;

// slot counters
RWByteAddressBuffer g_CounterBuffer;

// Inputs for spawn (FillNewPools only)
Texture2D<float> g_HeightField;
Texture2D<float> g_DensityField;
Texture2D<float> g_InteractionField;

// ---------------------------------------------------------------------------
// Constants / helpers
// ---------------------------------------------------------------------------
static const uint INVALID_U32 = 0xFFFFFFFFu;

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
// Free-list stack ops (counter in byteaddress at offset 0)
// ---------------------------------------------------------------------------
uint FreeList_Pop()
{
    // old = counter, counter -= 1
    uint old = 0u;
    g_FreeListCounter.InterlockedAdd(0, -1, old);

    if (old == 0u)
    {
        // underflow -> restore?
        // best effort: put counter back
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
    // old was previous count, now slot is [old]
    g_FreeList[old].Value = value;
}

// ---------------------------------------------------------------------------
//  Entry A) UpdateChunkPoolsCS
// ---------------------------------------------------------------------------
// One thread per visible cell.
// It compares desired chunkCoord vs current cell's chunkCoord; if mismatch, release old pool and alloc new.
[numthreads(8, 8, 1)]
void UpdateChunkPoolsCS(uint3 tid : SV_DispatchThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    if (tid.x >= dim || tid.y >= dim)
        return;

    uint cellIndex = tid.y * dim + tid.x;

    // Compute desired worldChunk based on camera
    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
    int2 camChunk = WorldXZToChunkCoord(camXZ, g_CB.ChunkSize);

    int halfExt = (int) g_CB.ChunkHalfExtent;
    int2 grid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);
    int2 desiredChunk = camChunk + grid;

    VisibleCell cell = g_VisibleCellTable[cellIndex];

    bool same = all(cell.ChunkCoord == desiredChunk) && (cell.PoolIndex != INVALID_U32);
    if (same)
        return;

    // If cell had pool, release it.
    if (cell.PoolIndex != INVALID_U32)
    {
        uint oldPool = cell.PoolIndex;

        // Mark pool dirty=0 (doesn't matter), chunkcoord invalid
        PoolChunkCoord pc = g_PoolChunkCoord[oldPool];
        pc.ChunkCoord = int2(0x80000000, 0x80000000);
        g_PoolChunkCoord[oldPool] = pc;

        PoolDirty pd = g_PoolDirty[oldPool];
        pd.Dirty = 0u;
        g_PoolDirty[oldPool] = pd;

        FreeList_Push(oldPool);
    }

    // Allocate a new pool
    uint newPool = FreeList_Pop();
    if (newPool == INVALID_U32)
    {
        // no pool available -> mark cell empty
        cell.PoolIndex = INVALID_U32;
        cell.ChunkCoord = desiredChunk;
        g_VisibleCellTable[cellIndex] = cell;
        return;
    }

    // Assign mapping
    cell.PoolIndex = newPool;
    cell.ChunkCoord = desiredChunk;
    g_VisibleCellTable[cellIndex] = cell;

    // Update pool's chunkcoord
    PoolChunkCoord pc2 = g_PoolChunkCoord[newPool];
    pc2.ChunkCoord = desiredChunk;
    g_PoolChunkCoord[newPool] = pc2;

    // Mark pool dirty => FillNewPoolsCS will spawn positions for it
    PoolDirty pd2 = g_PoolDirty[newPool];
    pd2.Dirty = 1u;
    g_PoolDirty[newPool] = pd2;
}

// ---------------------------------------------------------------------------
// Entry B) FillNewPoolsCS
// ---------------------------------------------------------------------------
// One thread-group per visible cell (thus per pool mapping), but it only works when pool is dirty.
// Each group fills SamplesPerChunk positions (looped by threadID).
//
// NOTE: spawn 비용(Height/Density/Interaction)은 여기서만 발생.
// ---------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void FillNewPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint cellIndex = gid.x;
    uint dim = g_CB.ChunkVisibleDim;
    uint visibleCells = dim * dim;

    if (cellIndex >= visibleCells)
        return;

    VisibleCell cell = g_VisibleCellTable[cellIndex];
    uint pool = cell.PoolIndex;
    if (pool == INVALID_U32)
        return;

    PoolDirty pd = g_PoolDirty[pool];
    if (pd.Dirty == 0u)
        return;

    // Only one lane clears dirty at the end (after fill)
    int2 chunkCoord = cell.ChunkCoord;
    float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);

    // Coarse per-chunk density cache
    float2 chunkCenterXZ = chunkOriginXZ + 0.5f * g_CB.ChunkSize.xx;
    float chunkDensity = SampleWorldDensity(chunkCenterXZ, 3.0f);

    // Early skip if empty: still clear dirty to avoid redoing
    if (chunkDensity <= 0.01f)
    {
        if (tid.x == 0u)
        {
            pd.Dirty = 0u;
            g_PoolDirty[pool] = pd;
        }
        return;
    }

    uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);
    uint base = pool * g_CB.SamplesPerChunk;

    // fill samples
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
            // mark invalid position using NaN y (cheap sentinel)
            g_PoolPositions[base + s] = float4(posXZ.x, asfloat(0x7FC00000u), posXZ.y, 0.0f);
            continue;
        }

        float press01 = saturate(SampleInteraction(posXZ));
        if (Rand01(seed ^ 0x41A7u) > density)
        {
            g_PoolPositions[base + s] = float4(posXZ.x, asfloat(0x7FC00000u), posXZ.y, 0.0f);
            continue;
        }

        float effectiveProb = saturate(g_CB.SpawnProb * density);
        if (Rand01(seed ^ 0x4444u) > effectiveProb)
        {
            g_PoolPositions[base + s] = float4(posXZ.x, asfloat(0x7FC00000u), posXZ.y, 0.0f);
            continue;
        }

        float y = SampleWorldHeight(posXZ);
        g_PoolPositions[base + s] = float4(posXZ.x, y, posXZ.y, press01);
    }

    // Clear dirty once (after everyone done)
    GroupMemoryBarrierWithGroupSync();
    if (tid.x == 0u)
    {
        pd.Dirty = 0u;
        g_PoolDirty[pool] = pd;
    }
}

// ---------------------------------------------------------------------------
// Wave helpers (for counter reserve)
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
// Entry C) BuildInstancesFromPoolsCS
// ---------------------------------------------------------------------------
// One thread-group per visible cell (pool)
// - First group (gid==0, tid==0) clears 3 counters
// - For each pool: iterate SamplesPerChunk positions, cheap LOD + pack + counters
//
// NOTE: 여기서는 Height/Density/Interaction 샘플링 없음.
// ---------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void BuildInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    uint visibleCells = dim * dim;

    // Clear counters once per dispatch (single lane)
    if (gid.x == 0u && tid.x == 0u)
    {
        uint off0 = (g_CB.IndirectSlotLOD0 << 2);
        uint off1 = (g_CB.IndirectSlotLOD1 << 2);
        uint off2 = (g_CB.IndirectSlotLOD2 << 2);

        // store 0
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

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);

    float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
    float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

    uint off0 = (g_CB.IndirectSlotLOD0 << 2);
    uint off1 = (g_CB.IndirectSlotLOD1 << 2);
    uint off2 = (g_CB.IndirectSlotLOD2 << 2);

    uint base = pool * g_CB.SamplesPerChunk;

    for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
    {
        float4 p = g_PoolPositions[base + s];

        // invalid sentinel: y is NaN
        if (isnan(p.y))
            continue;

        float3 posWS = float3(p.x, p.y, p.z);
        float press01 = saturate(p.w);

        float2 dxz = float2(posWS.x, posWS.z) - camXZ;
        float distSqr = dot(dxz, dxz);

        // quick scale/yaw/pitch/bend from deterministic hash of (pool,s)
        uint seed = WangHash(Hash2i(cell.ChunkCoord, g_CB.SeedSalt) ^ (s * 0x9E3779B9u));

        float scaleT = Rand01(seed ^ 0x5555u);
        float scale =
            lerp(g_CB.MinScale, g_CB.MaxScale, scaleT) *
            0.04f;

        float yaw = Rand01(seed ^ 0x6666u) * GRASS_TWO_PI;
        float pitch = lerp(g_CB.MinPitch, g_CB.MaxPitch, Rand01(seed ^ 0x7777u));
        float bend01 = lerp(g_CB.BendStrengthMin, g_CB.BendStrengthMax, Rand01(seed ^ 0x8888u));

        uint seed8 = (WangHash(seed ^ 0x1234u) >> 24) & 0xFFu;
        uint variantId = (WangHash(seed ^ 0xBEEFu) >> 30) & 0x3u;
        uint atlasIndex = (WangHash(seed ^ 0xCAFEu) >> 29) & 0x7u;

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
        else
        {
            uint pfx = BallotPrefixCountBits(b2, lane);

            GrassBillboardInstance inst = MakeGrassBillboardInstance(
                posWS, scale, yaw, atlasIndex, seed8);

            g_OutInstancesLOD2[base2 + pfx] = inst;
        }
    }
}
