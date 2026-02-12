#include "Common.hlsli"
#include "HeightField.hlsli"
#include "GrassCommon.hlsli"

cbuffer GRASS_GEN_CONSTANTS
{
    GrassGenConstants g_CB;
};

// ---------------------------------------------------------------------------
// Structured layouts (16-byte stride everywhere)
// ---------------------------------------------------------------------------

struct VisibleCell
{
    uint PoolIndex;
    int2 ChunkCoord;
    uint _pad;
};

struct PoolChunkCoord
{
    uint _pad0;
    int2 ChunkCoord;
    float ChunkHeight;
};

struct PoolDirty
{
    uint Dirty; // 0 clean, 1 needs fill, 2 filling
    uint3 _pad;
};

// Species mesh mapping: per species -> (lod0MeshId, lod1MeshId, lod2MeshId, pad)
StructuredBuffer<uint4> g_SpeciesMeshIdTable;

// ---------------------------------------------------------------------------
// UAVs / SRVs
// ---------------------------------------------------------------------------

RWStructuredBuffer<VisibleCell> g_VisibleCellTable;
RWStructuredBuffer<PoolChunkCoord> g_PoolChunkCoord;
RWStructuredBuffer<PoolDirty> g_PoolDirty;

// PoolPositions: float4(x, y, z, speciesId_as_float_bits)
RWStructuredBuffer<float4> g_PoolPositions;

// Rendering outputs (one buffer per LOD)
RWStructuredBuffer<GrassMeshInstance> g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance> g_OutInstancesLOD2;

// Per-mesh counters / offsets / scatter
RWByteAddressBuffer g_MeshInstanceCountBuffer; // uint per mesh (count)
RWByteAddressBuffer g_MeshInstanceOffsetBuffer; // uint per mesh (exclusive prefix)
RWByteAddressBuffer g_MeshScatterCounterBuffer; // uint per mesh (scatter allocator)

// Inputs
Texture2D<float> g_HeightField;
Texture2D<float> g_DensityField;
Texture2D<float> g_InteractionField;

// ---------------------------------------------------------------------------
// Constants / helpers
// ---------------------------------------------------------------------------

static const uint INVALID_U32 = 0xFFFFFFFFu;
static const float INVALID_NAN = asfloat(0x7FC00000u);

static const float CHUNK_DENSITY_MIP = 3.0f;
static const float DENSITY_DISABLE_THRESHOLD = 0.01f;
static const uint SAMPLES_MIN_FAR = 16u;
static const uint SAMPLES_MAX_CLAMP = 4096u;

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
    {
        return false;
    }

    chunkOriginXZ = clamp(chunkOriginXZ, hfMin - chunkSize.xx, hfMax);
    return true;
}

// ---------------------------------------------------------------------------
// Height / Density / Interaction sampling
// ---------------------------------------------------------------------------

float2 WorldXZToHeightUV(float2 worldXZ)
{
    return HF_WorldXZToUV(worldXZ, float2(1.0f, 1.0f), float2(0.0f, 0.0f));
}

float SampleHeightNormalized(float2 worldXZ)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    return HF_SampleHeight01(g_HeightField, g_LinearClampSampler, uv);
}

float SampleWorldHeight(float2 worldXZ)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    return HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uv, g_CB.YOffset);
}

float RemapDensity(float d, float contrast01)
{
    float a = saturate(contrast01);
    a = min(a, 0.49f);
    return smoothstep(a, 1.0f - a, d);
}

float SampleWorldDensity(float2 worldXZ, float mipLevel)
{
    float2 uv = WorldXZToHeightUV(worldXZ);
    float d = g_DensityField.SampleLevel(g_LinearWrapSampler, uv, mipLevel).r;
    d = saturate(d);
    d = RemapDensity(d, g_CB.DensityContrast);
    d = pow(d, g_CB.DensityPow);
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
        {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Distance-based sample scaling
// ---------------------------------------------------------------------------

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

    target = max(target, SAMPLES_MIN_FAR);
    target = min(target, min(baseSamples, SAMPLES_MAX_CLAMP));
    return target;
}

// ---------------------------------------------------------------------------
// Mesh counter helpers (simple interlocked)
// ---------------------------------------------------------------------------

uint MeshCount_ReserveOne(uint meshId)
{
    uint old = 0u;
    g_MeshScatterCounterBuffer.InterlockedAdd(meshId * 4u, 1u, old);
    return old;
}

void MeshCount_AddOne(uint meshId)
{
    uint ignored = 0u;
    g_MeshInstanceCountBuffer.InterlockedAdd(meshId * 4u, 1u, ignored);
}

uint LoadMeshOffset(uint meshId)
{
    return g_MeshInstanceOffsetBuffer.Load(meshId * 4u);
}

// ---------------------------------------------------------------------------
// Species id packing
// ---------------------------------------------------------------------------

uint DecodeSpeciesId(float w)
{
    return asuint(w);
}

float EncodeSpeciesId(uint speciesId)
{
    return asfloat(speciesId);
}

// ---------------------------------------------------------------------------
// Entry A) UpdateChunkPoolsCS
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void UpdateChunkPoolsCS(uint3 tid : SV_DispatchThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    if (tid.x >= dim || tid.y >= dim)
    {
        return;
    }

    uint cellIndex = tid.y * dim + tid.x;
    uint pool = cellIndex;

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
    int2 camChunk = WorldXZToChunkCoord(camXZ, g_CB.ChunkSize);

    int halfExt = (int) g_CB.ChunkHalfExtent;
    int2 grid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);
    int2 desiredChunk = camChunk + grid;

    VisibleCell cell = g_VisibleCellTable[cellIndex];
    cell.PoolIndex = pool;

    if (all(cell.ChunkCoord == desiredChunk))
    {
        g_VisibleCellTable[cellIndex] = cell;
        return;
    }

    cell.ChunkCoord = desiredChunk;
    g_VisibleCellTable[cellIndex] = cell;

    PoolChunkCoord pc = g_PoolChunkCoord[pool];
    pc.ChunkCoord = desiredChunk;
    pc.ChunkHeight = 0.0f;
    g_PoolChunkCoord[pool] = pc;

    PoolDirty pd = g_PoolDirty[pool];
    pd.Dirty = 1u;
    g_PoolDirty[pool] = pd;
}

// ---------------------------------------------------------------------------
// Entry B) FillNewPoolsCS
//  - NO thinning
//  - store speciesId in PoolPositions.w
// ---------------------------------------------------------------------------

[numthreads(256, 1, 1)]
void FillNewPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    uint visibleCells = dim * dim;

    uint cellIndex = gid.x;
    if (cellIndex >= visibleCells)
    {
        return;
    }

    uint pool = cellIndex;

    PoolDirty pd = g_PoolDirty[pool];
    if (pd.Dirty != 1u)
    {
        return;
    }

    if (tid.x == 0u)
    {
        pd.Dirty = 2u;
        g_PoolDirty[pool] = pd;
    }
    GroupMemoryBarrierWithGroupSync();

    VisibleCell cell = g_VisibleCellTable[cellIndex];
    int2 chunkCoord = cell.ChunkCoord;

    float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);

    if (!ClampChunkToHeightfield(chunkOriginXZ, g_CB.ChunkSize))
    {
        uint base = pool * g_CB.SamplesPerChunk;

        for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
        {
            g_PoolPositions[base + s] = float4(0.0f, INVALID_NAN, 0.0f, EncodeSpeciesId(0u));
        }

        GroupMemoryBarrierWithGroupSync();
        if (tid.x == 0u)
        {
            pd.Dirty = 0u;
            g_PoolDirty[pool] = pd;
        }
        return;
    }

    float chunkHeight = SampleWorldHeight(chunkOriginXZ);

    if (tid.x == 0u)
    {
        PoolChunkCoord pc = g_PoolChunkCoord[pool];
        pc.ChunkCoord = chunkCoord;
        pc.ChunkHeight = chunkHeight;
        g_PoolChunkCoord[pool] = pc;
    }

    uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);
    uint base = pool * g_CB.SamplesPerChunk;

    const uint numSpecies = max(1u, g_CB.NumSpecies);

    for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
    {
        uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

        float ux = Rand01(seed ^ 0x2222u);
        float uz = Rand01(seed ^ 0x3333u);

        float jx = (ux - 0.5f) * g_CB.Jitter;
        float jz = (uz - 0.5f) * g_CB.Jitter;

        float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * g_CB.ChunkSize;
        float2 posXZ = chunkOriginXZ + localXZ;

        float y = SampleWorldHeight(posXZ);

        // Uniform probability: 1/n (seed % NumSpecies)
        uint speciesId = (WangHash(seed ^ 0xABCD1234u) % numSpecies);

        g_PoolPositions[base + s] = float4(posXZ.x, y, posXZ.y, EncodeSpeciesId(speciesId));
    }

    GroupMemoryBarrierWithGroupSync();
    if (tid.x == 0u)
    {
        pd.Dirty = 0u;
        g_PoolDirty[pool] = pd;
    }
}

// ---------------------------------------------------------------------------
// Common: evaluate chunk and loop range
// ---------------------------------------------------------------------------

struct ChunkEval
{
    bool Valid;
    int2 ChunkCoord;
    float2 ChunkOriginClamped;
    float2 ChunkCenterXZ;
    float distChunkSqr;
    float chunkDensity;
    uint samplesThisChunk;
};

ChunkEval EvaluateChunk(uint cellIndex)
{
    ChunkEval e;
    e.Valid = false;

    uint pool = cellIndex;
    VisibleCell cell = g_VisibleCellTable[cellIndex];
    e.ChunkCoord = cell.ChunkCoord;

    float2 chunkOriginXZ = ChunkCoordToWorldOrigin(e.ChunkCoord, g_CB.ChunkSize);
    e.ChunkOriginClamped = chunkOriginXZ;

    if (!ClampChunkToHeightfield(e.ChunkOriginClamped, g_CB.ChunkSize))
    {
        return e;
    }

    PoolChunkCoord pc = g_PoolChunkCoord[pool];
    float chunkHeight = pc.ChunkHeight;
    if (chunkHeight == 0.0f)
    {
        chunkHeight = SampleWorldHeight(e.ChunkOriginClamped);
    }

    float3 chunkMin = float3(e.ChunkOriginClamped.x, chunkHeight - 120.0f, e.ChunkOriginClamped.y);
    float3 chunkMax = float3(e.ChunkOriginClamped.x + g_CB.ChunkSize, chunkHeight + 120.0f, e.ChunkOriginClamped.y + g_CB.ChunkSize);

    if (!AabbInsideFrustum(chunkMin, chunkMax))
    {
        return e;
    }

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
    e.ChunkCenterXZ = e.ChunkOriginClamped + 0.5f * g_CB.ChunkSize.xx;

    float2 dChunk = e.ChunkCenterXZ - camXZ;
    e.distChunkSqr = dot(dChunk, dChunk);

    float spawnRadiusSqr = g_CB.SpawnRadius * g_CB.SpawnRadius;
    if (e.distChunkSqr > spawnRadiusSqr)
    {
        return e;
    }

    e.chunkDensity = SampleWorldDensity(e.ChunkCenterXZ, CHUNK_DENSITY_MIP);
    if (e.chunkDensity <= DENSITY_DISABLE_THRESHOLD)
    {
        return e;
    }

    float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
    uint baseSamples = g_CB.SamplesPerChunk;
    e.samplesThisChunk = ComputeSamplesPerChunk(baseSamples, e.distChunkSqr, lod0Sqr, spawnRadiusSqr);

    uint densityScaled = (uint) round((float) e.samplesThisChunk * saturate(e.chunkDensity));
    e.samplesThisChunk = max(densityScaled, (uint) min(SAMPLES_MIN_FAR, e.samplesThisChunk));
    e.samplesThisChunk = min(e.samplesThisChunk, baseSamples);

    if (e.samplesThisChunk == 0u)
    {
        return e;
    }

    e.Valid = true;
    return e;
}

// ---------------------------------------------------------------------------
// Entry C) CountInstancesFromPoolsCS
//  - only increments g_MeshInstanceCountBuffer[meshId]
// ---------------------------------------------------------------------------

[numthreads(256, 1, 1)]
void CountInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    uint visibleCells = dim * dim;

    uint cellIndex = gid.x;
    if (cellIndex >= visibleCells)
    {
        return;
    }

    ChunkEval e = EvaluateChunk(cellIndex);
    if (!e.Valid)
    {
        return;
    }

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);

    float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
    float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

    uint base = cellIndex * g_CB.SamplesPerChunk;
    uint chunkSeed = Hash2i(e.ChunkCoord, g_CB.SeedSalt);

    float spawnGateBase = saturate(g_CB.SpawnProb * e.chunkDensity);

    for (uint s = tid.x; s < e.samplesThisChunk; s += 256u)
    {
        float4 p = g_PoolPositions[base + s];
        if (isnan(p.y))
        {
            continue;
        }

        uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

        if (Rand01(seed ^ 0x4444u) > spawnGateBase)
        {
            continue;
        }

        float2 posXZ = float2(p.x, p.z);

        float hN = SampleHeightNormalized(posXZ);
        float heightMask = ComputeHeightMask(hN);
        if (heightMask <= 0.001f)
        {
            continue;
        }

        float spawnGate = spawnGateBase * heightMask;
        if (spawnGate <= 0.001f)
        {
            continue;
        }

        if (Rand01(seed ^ 0x4A4Au) > spawnGate)
        {
            continue;
        }

        float2 dxz = float2(p.x, p.z) - camXZ;
        float distSqr = dot(dxz, dxz);

        bool emit0 = (distSqr < lod0Sqr);
        bool emit1 = (!emit0) && (distSqr < lod1Sqr);
        bool emit2 = (!emit0) && (!emit1);

        uint speciesId = DecodeSpeciesId(p.w);
        uint4 meshIds = g_SpeciesMeshIdTable[speciesId];

        uint meshId = emit0 ? meshIds.x : (emit1 ? meshIds.y : meshIds.z);
        MeshCount_AddOne(meshId);
    }
}

// ---------------------------------------------------------------------------
// Entry D) BuildInstancesFromPoolsCS
//  - uses g_MeshInstanceOffsetBuffer + g_MeshScatterCounterBuffer to pack into single LOD buffers
// ---------------------------------------------------------------------------

[numthreads(256, 1, 1)]
void BuildInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint dim = g_CB.ChunkVisibleDim;
    uint visibleCells = dim * dim;

    uint cellIndex = gid.x;
    if (cellIndex >= visibleCells)
    {
        return;
    }

    ChunkEval e = EvaluateChunk(cellIndex);
    if (!e.Valid)
    {
        return;
    }

    float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);

    float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
    float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

    uint base = cellIndex * g_CB.SamplesPerChunk;
    uint chunkSeed = Hash2i(e.ChunkCoord, g_CB.SeedSalt);

    float spawnGateBase = saturate(g_CB.SpawnProb * e.chunkDensity);

    for (uint s = tid.x; s < e.samplesThisChunk; s += 256u)
    {
        float4 p = g_PoolPositions[base + s];
        if (isnan(p.y))
        {
            continue;
        }

        uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

        if (Rand01(seed ^ 0x4444u) > spawnGateBase)
        {
            continue;
        }

        float2 posXZ = float2(p.x, p.z);

        float hN = SampleHeightNormalized(posXZ);
        float heightMask = ComputeHeightMask(hN);
        if (heightMask <= 0.001f)
        {
            continue;
        }

        float spawnGate = spawnGateBase * heightMask;
        if (spawnGate <= 0.001f)
        {
            continue;
        }

        float press01 = saturate(SampleInteraction(posXZ));

        if (Rand01(seed ^ 0x4A4Au) > spawnGate)
        {
            continue;
        }

        float3 posWS = float3(p.x, p.y, p.z);

        float2 dxz = float2(posWS.x, posWS.z) - camXZ;
        float distSqr = dot(dxz, dxz);

        float scaleT = Rand01(seed ^ 0x5555u);
        float scale = lerp(g_CB.MinScale, g_CB.MaxScale, scaleT);

        float yaw = Rand01(seed ^ 0x6666u) * GRASS_TWO_PI;
        float pitch = lerp(g_CB.MinPitch, g_CB.MaxPitch, Rand01(seed ^ 0x7777u));
        float bend01 = lerp(g_CB.BendStrengthMin, g_CB.BendStrengthMax, Rand01(seed ^ 0x8888u));

        uint seed8 = MakeSeed8(seed ^ 0x1234u);
        uint variantId = MakeVariantId(seed);
        uint atlasIndex = MakeAtlasIndex(seed);

        bool emit0 = (distSqr < lod0Sqr);
        bool emit1 = (!emit0) && (distSqr < lod1Sqr);
        bool emit2 = (!emit0) && (!emit1);

        uint speciesId = DecodeSpeciesId(p.w);
        uint4 meshIds = g_SpeciesMeshIdTable[speciesId];

        uint meshId = emit0 ? meshIds.x : (emit1 ? meshIds.y : meshIds.z);

        uint meshBase = LoadMeshOffset(meshId);
        uint local = MeshCount_ReserveOne(meshId);
        uint outIndex = meshBase + local;

        if (emit0)
        {
            GrassMeshInstance inst = MakeGrassMeshInstance(posWS, scale, yaw, pitch, bend01, press01, variantId, seed8);
            g_OutInstancesLOD0[outIndex] = inst;
        }
        else if (emit1)
        {
            GrassCrossPlaneInstance inst = MakeGrassCrossPlaneInstance(posWS, scale, yaw, bend01, press01, variantId, seed8, 0u, 0u);
            g_OutInstancesLOD1[outIndex] = inst;
        }
        else
        {
            GrassBillboardInstance inst = MakeGrassBillboardInstance(posWS, scale, yaw, atlasIndex, seed8);
            g_OutInstancesLOD2[outIndex] = inst;
        }
    }
}
