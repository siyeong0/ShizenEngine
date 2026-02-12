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
	uint Dirty; // 0=Clean, 1=NeedsFill, 2=Filling
	uint3 _pad;
};

// ---------------------------------------------------------------------------
// UAVs / SRVs
// ---------------------------------------------------------------------------
RWStructuredBuffer<VisibleCell> g_VisibleCellTable;
RWStructuredBuffer<PoolChunkCoord> g_PoolChunkCoord;
RWStructuredBuffer<PoolDirty> g_PoolDirty;

// PoolEntries
struct PoolEntry
{
	float3 Position;
	uint SpeciesId;
};
RWStructuredBuffer<PoolEntry> g_PoolPositions;

// Rendering outputs
RWStructuredBuffer<GrassMeshInstance> g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance> g_OutInstancesLOD2;

// Per-mesh counter (IndirectArgs mesh-based)
RWByteAddressBuffer g_MeshInstanceCountBuffer;

// Species/LOD packing buffers
// Layout: uint[MaxSpecies * 3], idx = speciesId*3 + lod
RWStructuredBuffer<uint> g_SpeciesLodCounts;
RWStructuredBuffer<uint> g_SpeciesLodOffsets;
RWStructuredBuffer<uint> g_SpeciesLodWriteCounters;

// Species -> MeshId lookup (SRV)
StructuredBuffer<uint> g_SpeciesLOD0MeshId;
StructuredBuffer<uint> g_SpeciesLOD1MeshId;
StructuredBuffer<uint> g_SpeciesLOD2MeshId;

// Inputs
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
// Distance-based sample scaling (used in Count/Build)
// ---------------------------------------------------------------------------
static const float CHUNK_DENSITY_MIP = 3.0f;
static const float DENSITY_DISABLE_THRESHOLD = 0.01f;
static const uint SAMPLES_MIN_FAR = 16u;
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

	target = max(target, SAMPLES_MIN_FAR);
	target = min(target, min(baseSamples, SAMPLES_MAX_CLAMP));
	return target;
}

// ---------------------------------------------------------------------------
// Mesh counter reserve helpers (wave optimized)
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
	{
		prefix += countbits(b.M.x);
	}
	if (word > 1u)
	{
		prefix += countbits(b.M.y);
	}
	if (word > 2u)
	{
		prefix += countbits(b.M.z);
	}

	uint w =
		(word == 0u) ? b.M.x :
		(word == 1u) ? b.M.y :
		(word == 2u) ? b.M.z : b.M.w;

	uint mask = (bit == 0u) ? 0u : ((1u << bit) - 1u);
	prefix += countbits(w & mask);
	return prefix;
}

uint WaveReserveMeshCounter(uint meshId, BallotMask ballot)
{
	uint base = 0u;
	uint cnt = BallotCountBits(ballot);

	if (cnt != 0u && WaveIsFirstLane())
	{
		uint byteOffset = meshId * 4u;
		g_MeshInstanceCountBuffer.InterlockedAdd(byteOffset, cnt, base);
	}
	return WaveReadLaneFirst(base);
}

// ---------------------------------------------------------------------------
// Per-instance random helpers
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
	return (WangHash(seed ^ 0xCAFEFu) >> 29) & 0x7u;
}

// ---------------------------------------------------------------------------
// Entry A) UpdateChunkPoolsCS (poolIndex == cellIndex)
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
// - NO THINNING
// - PoolPositions.w = asfloat(speciesId)
// - speciesId picked uniformly: seed % NumSpecies
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
			g_PoolPositions[base + s].Position = float3(0.0, INVALID_NAN, 0.0);
			g_PoolPositions[base + s].SpeciesId = 0;
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

	uint numSpecies = max(g_CB.NumSpecies, 1u);

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

		uint speciesId = WangHash(seed ^ 0xABCDEFu) % numSpecies;

		// w stores speciesId (as uint bits)
		g_PoolPositions[base + s].Position = float3(posXZ.x, y, posXZ.y);
		g_PoolPositions[base + s].SpeciesId = speciesId;
	}

	GroupMemoryBarrierWithGroupSync();
	if (tid.x == 0u)
	{
		pd.Dirty = 0u;
		g_PoolDirty[pool] = pd;
	}
}

// ---------------------------------------------------------------------------
// Entry B-1) ClearSpeciesCountersCS
// - Clears counts + offsets + write counters (optional safety)
// ---------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void ClearSpeciesCountersCS(uint3 tid : SV_DispatchThreadID)
{
	uint n = MAX_GRASS_SPECIES * 3u;
	if (tid.x >= n)
	{
		return;
	}

	g_SpeciesLodCounts[tid.x] = 0u;
	g_SpeciesLodOffsets[tid.x] = 0u;
	g_SpeciesLodWriteCounters[tid.x] = 0u;
}

// ---------------------------------------------------------------------------
// Entry B-2) CountInstancesFromPoolsCS
// - Uses same thinning rules as Build
// - Writes g_SpeciesLodCounts[species*3 + lod] ++
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

	uint pool = cellIndex;

	VisibleCell cell = g_VisibleCellTable[cellIndex];
	int2 chunkCoord = cell.ChunkCoord;

	float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);

	float2 chunkOriginClamped = chunkOriginXZ;
	if (!ClampChunkToHeightfield(chunkOriginClamped, g_CB.ChunkSize))
	{
		return;
	}

	PoolChunkCoord pc = g_PoolChunkCoord[pool];
	float chunkHeight = pc.ChunkHeight;
	if (chunkHeight == 0.0f)
	{
		chunkHeight = SampleWorldHeight(chunkOriginClamped);
	}

	float3 chunkMin = float3(chunkOriginClamped.x, chunkHeight - 120.0f, chunkOriginClamped.y);
	float3 chunkMax = float3(chunkOriginClamped.x + g_CB.ChunkSize, chunkHeight + 120.0f, chunkOriginClamped.y + g_CB.ChunkSize);

	if (!AabbInsideFrustum(chunkMin, chunkMax))
	{
		return;
	}

	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 chunkCenterXZ = chunkOriginClamped + 0.5f * g_CB.ChunkSize.xx;

	float2 dChunk = chunkCenterXZ - camXZ;
	float distChunkSqr = dot(dChunk, dChunk);

	float spawnRadiusSqr = g_CB.SpawnRadius * g_CB.SpawnRadius;
	if (distChunkSqr > spawnRadiusSqr)
	{
		return;
	}

	float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
	float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

	float chunkDensity = SampleWorldDensity(chunkCenterXZ, CHUNK_DENSITY_MIP);
	if (chunkDensity <= DENSITY_DISABLE_THRESHOLD)
	{
		return;
	}

	uint baseSamples = g_CB.SamplesPerChunk;
	uint samplesThisChunk = ComputeSamplesPerChunk(baseSamples, distChunkSqr, lod0Sqr, spawnRadiusSqr);

	uint densityScaled = (uint) round((float) samplesThisChunk * saturate(chunkDensity));
	samplesThisChunk = max(densityScaled, (uint) min(SAMPLES_MIN_FAR, samplesThisChunk));
	samplesThisChunk = min(samplesThisChunk, baseSamples);

	if (samplesThisChunk == 0u)
	{
		return;
	}

	uint base = pool * g_CB.SamplesPerChunk;

	for (uint s = tid.x; s < samplesThisChunk; s += 256u)
	{
		PoolEntry p = g_PoolPositions[base + s];

		// ------------------------------------------------------------
		// Early reject invalid pool position
		// ------------------------------------------------------------
		if (isnan(p.Position.y))
		{
			continue;
		}

		// SpeciesId stored in poolPos.w (uint bits)
		uint speciesId = p.SpeciesId;
		if (speciesId >= g_CB.NumSpecies)
		{
			continue;
		}

		// ------------------------------------------------------------
		// Stable seed per (chunkCoord, sampleIndex)
		// ------------------------------------------------------------
		uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);
		uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

		// ------------------------------------------------------------
		// Spawn gate (cheap first)
		// ------------------------------------------------------------
		float spawnGateBase = saturate(g_CB.SpawnProb * chunkDensity);

		if (Rand01(seed ^ 0x4444u) > spawnGateBase)
		{
			continue;
		}

		float2 posXZ = p.Position.xz;

		// Height band mask (1 height tap)
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

		// Interaction press (1 tap)
		float press01 = saturate(SampleInteraction(posXZ));

		// Final gate
		if (Rand01(seed ^ 0x4A4Au) > spawnGate)
		{
			continue;
		}

		// ------------------------------------------------------------
		// LOD decision (distance to camera)
		// ------------------------------------------------------------
		float3 posWS = p.Position;

		float2 dxz = posWS.xz - camXZ;
		float distSqr = dot(dxz, dxz);

		uint lodIndex = (distSqr < lod0Sqr) ? 0u : ((distSqr < lod1Sqr) ? 1u : 2u);

		// ------------------------------------------------------------
		// Count: species x lod
		// Layout: idx = speciesId * 3 + lodIndex
		// ------------------------------------------------------------
		uint idx = speciesId * 3u + lodIndex;
		InterlockedAdd(g_SpeciesLodCounts[idx], 1u);
	}

}

// ---------------------------------------------------------------------------
// Entry B-3) PrefixSpeciesOffsetsCS
// - Exclusive scan per LOD across species
// - Writes g_SpeciesLodOffsets and clears g_SpeciesLodWriteCounters
// ---------------------------------------------------------------------------
[numthreads(1, 1, 1)]
void PrefixSpeciesOffsetsCS(uint3 tid : SV_DispatchThreadID)
{
	uint numSpecies = min(max(g_CB.NumSpecies, 1u), (uint) MAX_GRASS_SPECIES);

	uint running0 = 0u;
	uint running1 = 0u;
	uint running2 = 0u;

	for (uint s = 0u; s < numSpecies; ++s)
	{
		uint i0 = s * 3u + 0u;
		uint i1 = s * 3u + 1u;
		uint i2 = s * 3u + 2u;

		uint c0 = g_SpeciesLodCounts[i0];
		uint c1 = g_SpeciesLodCounts[i1];
		uint c2 = g_SpeciesLodCounts[i2];

		g_SpeciesLodOffsets[i0] = running0;
		g_SpeciesLodOffsets[i1] = running1;
		g_SpeciesLodOffsets[i2] = running2;

		g_SpeciesLodWriteCounters[i0] = 0u;
		g_SpeciesLodWriteCounters[i1] = 0u;
		g_SpeciesLodWriteCounters[i2] = 0u;

		running0 += c0;
		running1 += c1;
		running2 += c2;
	}
}

// ---------------------------------------------------------------------------
// Entry C) BuildInstancesFromPoolsCS
// - THINNING HERE
// - Packs by species using offsets + writeCounters
// - Still increments mesh-based counters for WriteIndirectArgs compatibility
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

	uint pool = cellIndex;

	VisibleCell cell = g_VisibleCellTable[cellIndex];
	int2 chunkCoord = cell.ChunkCoord;

	float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);

	float2 chunkOriginClamped = chunkOriginXZ;
	if (!ClampChunkToHeightfield(chunkOriginClamped, g_CB.ChunkSize))
	{
		return;
	}

	PoolChunkCoord pc = g_PoolChunkCoord[pool];
	float chunkHeight = pc.ChunkHeight;
	if (chunkHeight == 0.0f)
	{
		chunkHeight = SampleWorldHeight(chunkOriginClamped);
	}

	float3 chunkMin = float3(chunkOriginClamped.x, chunkHeight - 120.0f, chunkOriginClamped.y);
	float3 chunkMax = float3(chunkOriginClamped.x + g_CB.ChunkSize, chunkHeight + 120.0f, chunkOriginClamped.y + g_CB.ChunkSize);

	if (!AabbInsideFrustum(chunkMin, chunkMax))
	{
		return;
	}

	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 chunkCenterXZ = chunkOriginClamped + 0.5f * g_CB.ChunkSize.xx;

	float2 dChunk = chunkCenterXZ - camXZ;
	float distChunkSqr = dot(dChunk, dChunk);

	float spawnRadiusSqr = g_CB.SpawnRadius * g_CB.SpawnRadius;
	if (distChunkSqr > spawnRadiusSqr)
	{
		return;
	}

	float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
	float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

	float chunkDensity = SampleWorldDensity(chunkCenterXZ, CHUNK_DENSITY_MIP);
	if (chunkDensity <= DENSITY_DISABLE_THRESHOLD)
	{
		return;
	}

	uint baseSamples = g_CB.SamplesPerChunk;
	uint samplesThisChunk = ComputeSamplesPerChunk(baseSamples, distChunkSqr, lod0Sqr, spawnRadiusSqr);

	uint densityScaled = (uint) round((float) samplesThisChunk * saturate(chunkDensity));
	samplesThisChunk = max(densityScaled, (uint) min(SAMPLES_MIN_FAR, samplesThisChunk));
	samplesThisChunk = min(samplesThisChunk, baseSamples);

	if (samplesThisChunk == 0u)
	{
		return;
	}

	uint base = pool * g_CB.SamplesPerChunk;

	for (uint s = tid.x; s < samplesThisChunk; s += 256u)
	{
		PoolEntry p = g_PoolPositions[base + s];

		// ------------------------------------------------------------
		// Early reject invalid pool position
		// ------------------------------------------------------------
		if (isnan(p.Position.y))
		{
			continue;
		}

		// SpeciesId stored in poolPos.w (uint bits)
		uint speciesId = p.SpeciesId;
		if (speciesId >= g_CB.NumSpecies)
		{
			continue;
		}

		// ------------------------------------------------------------
		// Stable seed per (chunkCoord, sampleIndex)
		// ------------------------------------------------------------
		uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);
		uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

		// ------------------------------------------------------------
		// Spawn gate (cheap first)
		// ------------------------------------------------------------
		float spawnGateBase = saturate(g_CB.SpawnProb * chunkDensity);

		if (Rand01(seed ^ 0x4444u) > spawnGateBase)
		{
			continue;
		}

		float2 posXZ = p.Position.xz;

		// Height band mask (1 height tap)
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

		// Interaction press (1 tap)
		float press01 = saturate(SampleInteraction(posXZ));

		// Final gate
		if (Rand01(seed ^ 0x4A4Au) > spawnGate)
		{
			continue;
		}

		// ------------------------------------------------------------
		// Material/random params
		// ------------------------------------------------------------
		float3 posWS = p.Position;

		float scaleT = Rand01(seed ^ 0x5555u);
		float scale = lerp(g_CB.MinScale, g_CB.MaxScale, scaleT);

		float yaw = Rand01(seed ^ 0x6666u) * GRASS_TWO_PI;
		float pitch = lerp(g_CB.MinPitch, g_CB.MaxPitch, Rand01(seed ^ 0x7777u));
		float bend01 = lerp(g_CB.BendStrengthMin, g_CB.BendStrengthMax, Rand01(seed ^ 0x8888u));

		uint seed8 = MakeSeed8(seed ^ 0x1234u);
		uint variantId = MakeVariantId(seed);
		uint atlasIndex = MakeAtlasIndex(seed);

		// ------------------------------------------------------------
		// LOD decision (distance to camera)
		// ------------------------------------------------------------
		float2 dxz = posWS.xz - camXZ;
		float distSqr = dot(dxz, dxz);

		uint lodIndex = (distSqr < lod0Sqr) ? 0u : ((distSqr < lod1Sqr) ? 1u : 2u);

		// Pick meshId by species + lod (keeps IndirectArgs flow unchanged)
		uint meshId =
			(lodIndex == 0u) ? g_SpeciesLOD0MeshId[speciesId] :
			(lodIndex == 1u) ? g_SpeciesLOD1MeshId[speciesId] :
							   g_SpeciesLOD2MeshId[speciesId];

		// Reserve mesh counter (wave optimized)
		bool emit0 = (lodIndex == 0u);
		bool emit1 = (lodIndex == 1u);
		bool emit2 = (lodIndex == 2u);

		uint byteOffset = meshId * 4u;
		uint oldValue;
		g_MeshInstanceCountBuffer.InterlockedAdd(byteOffset, 1u, oldValue);

		// Species packing: atomic add per (species,lod) to get local index
		uint idx = speciesId * 3u + lodIndex;

		uint local = 0u;
		InterlockedAdd(g_SpeciesLodWriteCounters[idx], 1u, local);

		uint globalIndex = g_SpeciesLodOffsets[idx] + local;

		if (lodIndex == 0u)
		{
			GrassMeshInstance inst = MakeGrassMeshInstance(posWS, scale, yaw, pitch, bend01, press01, variantId, seed8);
			g_OutInstancesLOD0[globalIndex] = inst;
		}
		else if (lodIndex == 1u)
		{
			GrassCrossPlaneInstance inst = MakeGrassCrossPlaneInstance(posWS, scale, yaw, bend01, press01, variantId, seed8, 0u, 0u);
			g_OutInstancesLOD1[globalIndex] = inst;
		}
		else
		{
			GrassBillboardInstance inst = MakeGrassBillboardInstance(posWS, scale, yaw, atlasIndex, seed8);
			g_OutInstancesLOD2[globalIndex] = inst;
		}
	}
}
