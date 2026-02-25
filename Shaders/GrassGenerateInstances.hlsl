// ============================================================================
// GrassGenerateInstances.hlsl
// - Base vs Special split
//   * Base: always eligible everywhere, picked by base weights
//   * Special: Voronoi cell chooses ONE special species, spawned by round-ish mask
//   * Inside special clusters: base probability reduced by BaseSuppressInPatch
// - Removed unused g_SpeciesFlags / g_TypeToSpecies requirements
// - Keeps your buffers/packing/prefix logic intact
// ============================================================================

#include "Common.hlsli"
#include "TerrainCommon.hlsli"
#include "GrassCommon.hlsli"

cbuffer GRASS_GEN_CONSTANTS
{
	GrassGenConstants g_CB;
};

// -----------------------------------------------------------------------------
// Structured layouts (16-byte stride everywhere)
// -----------------------------------------------------------------------------
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

RWStructuredBuffer<VisibleCell> g_VisibleCellTable;
RWStructuredBuffer<PoolChunkCoord> g_PoolChunkCoord;
RWStructuredBuffer<PoolDirty> g_PoolDirty;

struct PoolEntry
{
	float3 Position;
	uint Seed; // per-point seed (NOT typeId)
};
RWStructuredBuffer<PoolEntry> g_PoolPositions;

// Outputs (STATIC variables in PSO on C++ side)
RWStructuredBuffer<GrassMeshInstance> g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance> g_OutInstancesLOD2;

// Per-mesh counter (ByteAddress) : meshId*4 = uint counter
RWByteAddressBuffer g_MeshInstanceCountBuffer;

// Type/LOD packing buffers
RWStructuredBuffer<uint> g_SpeciesLodCounts;
RWStructuredBuffer<uint> g_SpeciesLodOffsets;
RWStructuredBuffer<uint> g_SpeciesLodWriteCounters;

StructuredBuffer<uint> g_SpeciesLOD0MeshId;
StructuredBuffer<uint> g_SpeciesLOD1MeshId;
StructuredBuffer<uint> g_SpeciesLOD2MeshId;

// Inputs
Texture2D<float> g_InteractionField;

// -----------------------------------------------------------------------------
// Species selection tables
// -----------------------------------------------------------------------------

// Base species (subset) selection
StructuredBuffer<float> g_BaseSpeciesWeightPrefix; // size >= NumBaseSpecies
StructuredBuffer<uint> g_BaseSpeciesIds; // size >= NumBaseSpecies (maps subset index -> speciesId)

// Special species (subset) selection (Voronoi cell -> 1 special species)
StructuredBuffer<float> g_SpecialSpeciesWeightPrefix; // size >= NumSpecialSpecies
StructuredBuffer<uint> g_SpecialSpeciesIds; // size >= NumSpecialSpecies

// Species variation tables (global, unchanged)
StructuredBuffer<uint> g_SpeciesVarOffsets; // size >= NumSpecies+1
StructuredBuffer<uint> g_SpeciesVarCounts; // size >= NumSpecies
StructuredBuffer<uint> g_SpeciesVarToTypeId; // flat index -> typeId

// Per-type params (minScale,maxScale,bendMin,bendMax)
StructuredBuffer<float4> g_TypeParams0;

// Per-species clustering params
// float4(ClusterStrength, ClusterScaleMeters, ClusterJitter01, BaseSuppressInPatch)
StructuredBuffer<float4> g_SpeciesClusterParams;

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
static const uint INVALID_U32 = 0xFFFFFFFFu;
static const float INVALID_NAN = asfloat(0x7FC00000u);

static const float CHUNK_DENSITY_MIP = 1.0f;
static const float INSTANCE_DENSITY_MIP = 0.0f;
static const float DENSITY_DISABLE_THRESHOLD = 0.01f;

static const float CHUNK_AABB_HALF_Y = 20.0f;

// Voronoi boundary gap tuning
static const float VC_GAP_ABS_MIN = 0.035f;
static const float VC_GAP_REL = 0.12f;

// Spawn density tuning
static const float CLUSTER_SPAWN_BOOST = 1.35f;
static const float MIN_FILL_IN_CELL = 0.30f;

// -----------------------------------------------------------------------------
// Hash / random
// -----------------------------------------------------------------------------
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

uint Hash2u(uint2 p)
{
	uint h = p.x * 1664525u + 1013904223u;
	h ^= p.y * 22695477u + 1u;
	h = WangHash(h);
	return h;
}

float Hash01(uint2 p)
{
	return (Hash2u(p) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float2 Hash02(uint2 p)
{
	uint h0 = Hash2u(p);
	uint h1 = WangHash(h0 ^ 0x9E3779B9u);
	float a = (h0 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	float b = (h1 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	return float2(a, b);
}

float2 Smooth2(float2 t)
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// -----------------------------------------------------------------------------
// Small helpers: rotation / isotropy
// -----------------------------------------------------------------------------
float2 Rotate2D(float2 v, float a)
{
	float s = sin(a);
	float c = cos(a);
	return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// -----------------------------------------------------------------------------
// Tiny procedural value noise (ONLY for mild domain warp)
// -----------------------------------------------------------------------------
float ValueNoise2D(float2 x, uint seed)
{
	float2 p = floor(x);
	float2 f = frac(x);

	uint2 i00 = (uint2) p + uint2(0, 0);
	uint2 i10 = (uint2) p + uint2(1, 0);
	uint2 i01 = (uint2) p + uint2(0, 1);
	uint2 i11 = (uint2) p + uint2(1, 1);

	float v00 = Hash01(i00 ^ uint2(seed, seed * 1664525u));
	float v10 = Hash01(i10 ^ uint2(seed, seed * 1664525u));
	float v01 = Hash01(i01 ^ uint2(seed, seed * 1664525u));
	float v11 = Hash01(i11 ^ uint2(seed, seed * 1664525u));

	float2 u = Smooth2(f);

	float a = lerp(v00, v10, u.x);
	float b = lerp(v01, v11, u.x);
	return lerp(a, b, u.y);
}

// -----------------------------------------------------------------------------
// Isotropic/Decorrelated domain warp
// -----------------------------------------------------------------------------
float2 DomainWarp2D_Isotropic(float2 x, float warpAmp, uint seed)
{
	if (warpAmp <= 0.0f)
		return x;

	float2 p = x;
	float2 w = 0.0f;

	float amp = 1.0f;
	float freq = 0.18f;

	[unroll]
	for (int o = 0; o < 3; ++o)
	{
		uint so = WangHash(seed ^ (0x9E3779B9u * (uint) (o + 1)));

		float a = Rand01(so ^ 0x1234u) * GRASS_TWO_PI;
		float2 pr = Rotate2D(p * freq, a);

		float nx = ValueNoise2D(pr + float2(11.13f, 7.77f), so ^ 0x1111u);
		float nz = ValueNoise2D(pr + float2(3.33f, 19.19f), so ^ 0x2222u);

		float2 ww = (float2(nx, nz) * 2.0f - 1.0f);
		float len = max(length(ww), 1e-3f);
		ww /= len;

		w += ww * amp;

		amp *= 0.5f;
		freq *= 2.03f;
	}

	return x + w * (warpAmp * 0.35f);
}

// -----------------------------------------------------------------------------
// WORLEY(Cellular) distance field + seed retrieval (Voronoi)
// -----------------------------------------------------------------------------
struct WorleyResult
{
	float F1;
	float F2;
	uint2 SeedCell;
	float2 SeedPos;
};

WorleyResult WorleyF1F2_WithSeed(float2 x, uint seed)
{
	float2 p = floor(x);
	float2 f = frac(x);

	float best1 = 1e9f;
	float best2 = 1e9f;

	uint2 bestCell = 0u;
	float2 bestPos = 0.0f;

	[unroll]
	for (int j = -1; j <= 1; ++j)
	{
		[unroll]
		for (int i = -1; i <= 1; ++i)
		{
			float2 cellF = p + float2(i, j);
			uint2 cell = (uint2) cellF;

			float2 rnd = Hash02(cell ^ uint2(seed, seed * 1664525u));
			float2 feature = float2(i, j) + rnd;
			float2 q = feature - f;
			float d2 = dot(q, q);

			if (d2 < best1)
			{
				best2 = best1;
				best1 = d2;
				bestCell = cell;
				bestPos = cellF + rnd;
			}
			else if (d2 < best2)
			{
				best2 = d2;
			}
		}
	}

	WorleyResult r;
	r.F1 = sqrt(best1);
	r.F2 = sqrt(best2);
	r.SeedCell = bestCell;
	r.SeedPos = bestPos;
	return r;
}

// -----------------------------------------------------------------------------
// Chunk mapping
// -----------------------------------------------------------------------------
int2 WorldXZToChunkCoord(float2 worldXZ, float chunkSize)
{
	float2 rel = worldXZ - g_TerrainCB.WorldOriginXZ;
	float inv = rcp(max(chunkSize, 1e-6f));
	float2 c = floor(rel * inv);
	return int2((int) c.x, (int) c.y);
}

float2 ChunkCoordToWorldOrigin(int2 chunkCoord, float chunkSize)
{
	return g_TerrainCB.WorldOriginXZ + float2(chunkCoord) * chunkSize;
}

bool ClampChunkToHeightfield(inout float2 chunkOriginXZ, float chunkSize)
{
	float2 hfMin = g_TerrainCB.WorldOriginXZ;
	float2 hfMax = g_TerrainCB.WorldOriginXZ + g_TerrainCB.WorldSizeXZ;

	float2 o = chunkOriginXZ;
	float2 e = o + chunkSize.xx;

	if (e.x < hfMin.x || e.y < hfMin.y || o.x > hfMax.x || o.y > hfMax.y)
		return false;

	chunkOriginXZ = clamp(chunkOriginXZ, hfMin - chunkSize.xx, hfMax);
	return true;
}

// -----------------------------------------------------------------------------
// Density / interaction
// -----------------------------------------------------------------------------
float RemapDensity(float d, float contrast01)
{
	float a = saturate(contrast01);
	a = min(a, 0.49f);
	return smoothstep(a, 1.0f - a, d);
}

float SampleWorldDensity(float2 worldXZ, float mipLevel)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);

	float d = SampleTerrainVegetationLevel(uv, mipLevel);
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

// -----------------------------------------------------------------------------
// Frustum culling
// -----------------------------------------------------------------------------
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

bool GrassInstanceAabbInsideFrustum(float3 posWS, float scale)
{
	float halfXZ = 0.5 * scale;
	float minY = -0.05 * scale;
	float maxY = 1.05 * scale;

	float3 bmin = posWS + float3(-halfXZ, minY, -halfXZ);
	float3 bmax = posWS + float3(halfXZ, maxY, halfXZ);

	float pad = 0.5f * scale;
	bmin -= pad.xxx;
	bmax += pad.xxx;

	return AabbInsideFrustum(bmin, bmax);
}

// -----------------------------------------------------------------------------
// Wave mesh counter reserve helpers (grouped by meshId)
// -----------------------------------------------------------------------------
struct BallotMask
{
	uint4 M;
};

BallotMask WaveBallotAll()
{
	BallotMask b;
	b.M = WaveActiveBallot(true);
	return b;
}
bool BallotAny(BallotMask b)
{
	return (b.M.x | b.M.y | b.M.z | b.M.w) != 0u;
}
BallotMask BallotAndNot(BallotMask a, BallotMask b)
{
	BallotMask r;
	r.M = uint4(a.M.x & ~b.M.x, a.M.y & ~b.M.y, a.M.z & ~b.M.z, a.M.w & ~b.M.w);
	return r;
}
uint BallotCountBits(BallotMask b)
{
	return countbits(b.M.x) + countbits(b.M.y) + countbits(b.M.z) + countbits(b.M.w);
}
bool BallotTestLane(BallotMask b, uint lane)
{
	uint word = lane >> 5;
	uint bit = lane & 31u;

	uint w =
		(word == 0u) ? b.M.x :
		(word == 1u) ? b.M.y :
		(word == 2u) ? b.M.z : b.M.w;

	return ((w >> bit) & 1u) != 0u;
}
uint BallotFirstLane(BallotMask b)
{
	if (b.M.x != 0u)
		return 0u + firstbitlow(b.M.x);
	if (b.M.y != 0u)
		return 32u + firstbitlow(b.M.y);
	if (b.M.z != 0u)
		return 64u + firstbitlow(b.M.z);
	return 96u + firstbitlow(b.M.w);
}

uint WaveReserveMeshCounter_Grouped(uint meshId)
{
	uint lane = WaveGetLaneIndex();
	BallotMask remaining = WaveBallotAll();

	uint myBase = 0u;

	[loop]
	while (BallotAny(remaining))
	{
		uint leaderLane = BallotFirstLane(remaining);
		uint leaderMesh = WaveReadLaneAt(meshId, leaderLane);

		BallotMask group;
		group.M = WaveActiveBallot(meshId == leaderMesh);

		uint cnt = BallotCountBits(group);

		uint base = 0u;
		if (lane == leaderLane)
		{
			uint byteOffset = leaderMesh * 4u;
			g_MeshInstanceCountBuffer.InterlockedAdd(byteOffset, cnt, base);
		}
		base = WaveReadLaneAt(base, leaderLane);

		if (BallotTestLane(group, lane))
			myBase = base;

		remaining = BallotAndNot(remaining, group);
	}

	return myBase;
}

// -----------------------------------------------------------------------------
// Variation mapping (global species -> typeId)
// -----------------------------------------------------------------------------
uint PickVariationUniform(uint speciesId, uint seed)
{
	uint num = max(g_SpeciesVarCounts[speciesId], 1u);
	return WangHash(seed ^ 0x7777u) % num;
}

uint MapSpeciesVariationToTypeId(uint speciesId, uint varId)
{
	uint base = g_SpeciesVarOffsets[speciesId];
	uint idx = base + varId;
	return g_SpeciesVarToTypeId[idx];
}

// -----------------------------------------------------------------------------
// ¡°Point sampling¡± threshold generator (stable, blue-noise-ish)
// -----------------------------------------------------------------------------
float StablePointThreshold01(float2 worldXZ, uint seed)
{
	const float freq = 0.35f;
	float2 g = worldXZ * freq;

	int2 ig = int2(floor(g));
	float2 f = frac(g);

	uint h = Hash2i(ig, seed ^ 0xA53A9E37u);
	float ang = Rand01(h ^ 0xBEEF1234u) * GRASS_TWO_PI;
	float s = sin(ang), c = cos(ang);

	float2 fr;
	fr.x = c * (f.x - 0.5f) - s * (f.y - 0.5f);
	fr.y = s * (f.x - 0.5f) + c * (f.y - 0.5f);
	fr += 0.5f;

	uint2 cell = uint2(ig) ^ uint2(seed, seed * 1664525u);
	float base = Hash01(cell);

	float r = saturate(length(fr - 0.5f.xx) * 1.41421356f);
	float intra = 1.0f - r;

	return saturate(base * 0.85f + intra * 0.15f);
}

// -----------------------------------------------------------------------------
// Shared spawn logic
// -----------------------------------------------------------------------------
struct ChunkContext
{
	int2 ChunkCoord;
	float2 ChunkOriginClampedXZ;
	float2 ChunkCenterXZ;
	float DistChunkSqr;
	float SpawnRadiusSqr;
	float Lod0Sqr;
	float Lod1Sqr;
	float ChunkDensity;
};

bool BuildChunkContext(uint cellIndex, out ChunkContext ctx)
{
	uint dim = g_CB.ChunkVisibleDim;
	uint visibleCells = dim * dim;
	if (cellIndex >= visibleCells)
		return false;

	VisibleCell cell = g_VisibleCellTable[cellIndex];
	int2 chunkCoord = cell.ChunkCoord;

	float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);
	float2 chunkOriginClamped = chunkOriginXZ;

	if (!ClampChunkToHeightfield(chunkOriginClamped, g_CB.ChunkSize))
		return false;

	float chunkHeight = g_PoolChunkCoord[cellIndex].ChunkHeight;
	if (chunkHeight == 0.0f)
		chunkHeight = SampleWorldHeightAtWorldXZLevel(chunkOriginClamped, 5.0f);

	float3 chunkMin = float3(chunkOriginClamped.x, chunkHeight - CHUNK_AABB_HALF_Y, chunkOriginClamped.y);
	float3 chunkMax = float3(chunkOriginClamped.x + g_CB.ChunkSize, chunkHeight + CHUNK_AABB_HALF_Y, chunkOriginClamped.y + g_CB.ChunkSize);

	float3 ex = float3(0.5, 0.5, 0.5);
	if (!AabbInsideFrustum(chunkMin - ex, chunkMax + ex))
		return false;

	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 chunkCenterXZ = chunkOriginClamped + 0.5f * g_CB.ChunkSize.xx;

	float2 dChunk = chunkCenterXZ - camXZ;
	float distChunkSqr = dot(dChunk, dChunk);

	float spawnRadiusSqr = g_CB.SpawnRadius * g_CB.SpawnRadius;
	if (distChunkSqr > spawnRadiusSqr)
		return false;

	float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
	float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

	float chunkDensity = SampleWorldDensity(chunkCenterXZ, CHUNK_DENSITY_MIP);
	if (chunkDensity <= DENSITY_DISABLE_THRESHOLD)
		return false;

	ctx.ChunkCoord = chunkCoord;
	ctx.ChunkOriginClampedXZ = chunkOriginClamped;
	ctx.ChunkCenterXZ = chunkCenterXZ;
	ctx.DistChunkSqr = distChunkSqr;
	ctx.SpawnRadiusSqr = spawnRadiusSqr;
	ctx.Lod0Sqr = lod0Sqr;
	ctx.Lod1Sqr = lod1Sqr;
	ctx.ChunkDensity = chunkDensity;

	return true;
}

float2 ComputeYawPitchFromNormal(float3 nWS, float yaw)
{
	nWS = normalize(nWS);

	float s = sin(yaw);
	float c = cos(yaw);

	float3 nL;
	nL.x = c * nWS.x + s * nWS.z;
	nL.y = nWS.y;
	nL.z = -s * nWS.x + c * nWS.z;

	float pitch = atan2(nL.z, max(nL.y, 1e-6f));
	return float2(yaw, pitch);
}

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

// -----------------------------------------------------------------------------
// Subset prefix picker (base / special)
// -----------------------------------------------------------------------------
uint PickSubsetIndexFromPrefix(
	StructuredBuffer<float> prefix,
	uint subsetCount,
	uint seed)
{
	uint n = max(subsetCount, 1u);
	float total = prefix[n - 1u];

	if (total <= 1e-8f)
		return WangHash(seed) % n;

	float r = Rand01(seed ^ 0x13579BDFu) * total;

	[loop]
	for (uint i = 0u; i < MAX_GRASS_SPECIES; ++i)
	{
		if (i >= n)
			break;
		if (r <= prefix[i])
			return i;
	}
	return n - 1u;
}

uint PickBaseSpecies(uint seed)
{
	uint nb = max(g_CB.NumBaseSpecies, 1u);
	uint idx = PickSubsetIndexFromPrefix(g_BaseSpeciesWeightPrefix, nb, seed ^ 0xA1B2C3D4u);
	return g_BaseSpeciesIds[idx];
}

uint PickSpecialSpecies(uint seed)
{
	uint ns = g_CB.NumSpecialSpecies;
	if (ns == 0u)
		return 0u;

	uint idx = PickSubsetIndexFromPrefix(g_SpecialSpeciesWeightPrefix, ns, seed ^ 0xDEADBEEFu);
	return g_SpecialSpeciesIds[idx];
}

// -----------------------------------------------------------------------------
// Special Voronoi cluster evaluation
// -----------------------------------------------------------------------------
struct SpecialClusterEval
{
	uint SpecialSpeciesId;
	float Mask; // 0..1
	float CellEdgeKeep; // 0..1
	float BaseSuppress; // 0..1
};

SpecialClusterEval EvaluateSpecialVoronoiCluster(float2 worldXZ)
{
	SpecialClusterEval o;
	o.SpecialSpeciesId = 0u;
	o.Mask = 0.0f;
	o.CellEdgeKeep = 1.0f;
	o.BaseSuppress = 0.0f;

	// no special species -> no cluster
	if (g_CB.NumSpecialSpecies == 0u)
		return o;

	const float MacroCellMeters = 6.0f;
	uint vorSeed = WangHash(g_CB.SeedSalt ^ 0xCAFEBABEu);

	// identity space (no warp/rotation)
	float2 x_id = (worldXZ - g_TerrainCB.WorldOriginXZ) / max(MacroCellMeters, 1e-3f);

	WorleyResult wr0 = WorleyF1F2_WithSeed(x_id, vorSeed);
	uint clusterHash = Hash2u(wr0.SeedCell ^ uint2(vorSeed, vorSeed * 1664525u));

	// pick ONE special species per cell (stable)
	uint spId = PickSpecialSpecies(clusterHash);
	float4 cp = g_SpeciesClusterParams[spId];

	float strength = saturate(cp.x);
	float jitter01 = saturate(cp.z);
	float suppress = saturate(cp.w);

	// mask space (cell-based transform)
	uint cellSeed = WangHash(clusterHash ^ 0x9E3779B9u);
	float2 x_mask = x_id;

	x_mask = Rotate2D(x_mask, Rand01(cellSeed ^ 0xABCDu) * GRASS_TWO_PI);
	x_mask = DomainWarp2D_Isotropic(x_mask, jitter01 * 0.22f, cellSeed);

	WorleyResult wr = WorleyF1F2_WithSeed(x_mask, vorSeed);

	float edge = saturate(wr.F2 - wr.F1);
	float gapW = max(VC_GAP_ABS_MIN, VC_GAP_REL * lerp(0.06f, 0.14f, strength));
	float edgeKeep = smoothstep(gapW, gapW * 2.0f, edge);

	float radius = lerp(0.84f, 0.58f, strength);
	float softness = lerp(0.24f, 0.12f, strength);

	float core = 1.0f - smoothstep(radius - softness, radius + softness, wr.F1);
	float k = lerp(1.0f, 2.4f, strength);

	float M = saturate(pow(saturate(core), k));
	M *= lerp(MIN_FILL_IN_CELL, 1.0f, edgeKeep);

	o.SpecialSpeciesId = spId;
	o.Mask = saturate(M);
	o.CellEdgeKeep = edgeKeep;
	o.BaseSuppress = suppress;

	return o;
}

// -----------------------------------------------------------------------------
// EvaluateSpawn: Special first, else Base with suppression inside clusters
// -----------------------------------------------------------------------------
bool EvaluateSpawn(
	ChunkContext ctx,
	uint poolBase,
	uint sampleIndex,
	out uint outTypeId,
	out uint outLodIndex,
	out float outPress01,
	out float outScale,
	out float outYaw,
	out float outPitch,
	out float outBend01,
	out uint outSeed8,
	out uint outVariantId,
	out uint outAtlasIndex,
	out float3 outPosWS
)
{
	outTypeId = 0u;
	outLodIndex = 0u;
	outPress01 = 0.0f;
	outScale = 0.0f;
	outYaw = 0.0f;
	outPitch = 0.0f;
	outBend01 = 0.0f;
	outSeed8 = 0u;
	outVariantId = 0u;
	outAtlasIndex = 0u;
	outPosWS = float3(0, 0, 0);

	PoolEntry p = g_PoolPositions[poolBase + sampleIndex];
	if (isnan(p.Position.y))
	{
		outPosWS = float3(0.0f, INVALID_NAN, 0.0f);
		return false;
	}

	float2 posXZ = p.Position.xz;

	float D = SampleWorldDensity(posXZ, INSTANCE_DENSITY_MIP);
	if (D <= DENSITY_DISABLE_THRESHOLD)
		return false;

	uint seed = WangHash(p.Seed ^ (sampleIndex * 0x9E3779B9u));

	// evaluate special cluster at this point (mask + suppress)
	SpecialClusterEval ce = EvaluateSpecialVoronoiCluster(posXZ);

	// Interaction / position
	float press01 = saturate(SampleInteraction(posXZ));
	float3 posWS = p.Position;

	// LOD
	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 dxz = posWS.xz - camXZ;
	float distSqr = dot(dxz, dxz);

	uint lodIndex =
		(distSqr < ctx.Lod0Sqr) ? 0u :
		(distSqr < ctx.Lod1Sqr) ? 1u : 2u;

	// base threshold (stable per point)
	float th = StablePointThreshold01(posXZ, seed ^ 0xC0FFEEu);

	// ---------------------------------------------------------------------
	// 1) SPECIAL spawn attempt (inside mask)
	// ---------------------------------------------------------------------
	bool spawnedSpecial = false;
	uint chosenSpecies = 0u;

	if (g_CB.NumSpecialSpecies > 0u && ce.Mask > 0.0001f)
	{
		float pSpecial = saturate(D * ce.Mask * CLUSTER_SPAWN_BOOST);

		// slightly harder near boundaries
		pSpecial *= lerp(0.85f, 1.0f, ce.CellEdgeKeep);

		if (pSpecial > 0.001f && th <= pSpecial)
		{
			chosenSpecies = ce.SpecialSpeciesId;
			spawnedSpecial = true;
		}
	}

	// ---------------------------------------------------------------------
	// 2) BASE spawn (everywhere), but reduced in special clusters
	// ---------------------------------------------------------------------
	if (!spawnedSpecial)
	{
		float reduce = saturate(ce.BaseSuppress * ce.Mask); // 0 outside cluster, high in core
		float pBase = saturate(D * (1.0f - reduce));

		if (pBase <= 0.001f || th > pBase)
			return false;

		chosenSpecies = PickBaseSpecies(seed);
	}

	// ---------------------------------------------------------------------
	// Species -> variation -> type
	// ---------------------------------------------------------------------
	uint varId = PickVariationUniform(chosenSpecies, seed ^ 0xBBBBu);
	uint typeId = MapSpeciesVariationToTypeId(chosenSpecies, varId);

	if (typeId >= g_CB.NumGrassTypes)
		typeId = typeId % max(g_CB.NumGrassTypes, 1u);

	float4 typeP = g_TypeParams0[typeId];
	float minScale = typeP.x;
	float maxScale = max(typeP.y, minScale);
	float bendMin = typeP.z;
	float bendMax = max(typeP.w, bendMin);

	float scaleT = Rand01(seed ^ 0x5555u);
	float scale = lerp(minScale, maxScale, scaleT);

	float yaw = Rand01(seed ^ 0x6666u) * GRASS_TWO_PI;

	float3 terrainN = normalize(SampleTerrainNormalAtWorldXZLevel(posXZ, 0.0f));
	float slope01 = saturate(1.0f - terrainN.y);
	float align = saturate(slope01) * saturate(g_CB.NormalAlignStrength);

	float2 yp = ComputeYawPitchFromNormal(terrainN, yaw);
	float pitch = yp.y * align;

	float bend01 = lerp(bendMin, bendMax, Rand01(seed ^ 0x8888u));

	uint seed8 = MakeSeed8(seed ^ 0x1234u);
	uint variantId = MakeVariantId(seed);
	uint atlasIndex = MakeAtlasIndex(seed);

	if (!GrassInstanceAabbInsideFrustum(posWS, scale))
		return false;

	outTypeId = typeId;
	outLodIndex = lodIndex;
	outPress01 = press01;
	outScale = scale;
	outYaw = yaw;
	outPitch = pitch;
	outBend01 = bend01;
	outSeed8 = seed8;
	outVariantId = variantId;
	outAtlasIndex = atlasIndex;
	outPosWS = posWS;

	return true;
}

// -----------------------------------------------------------------------------
// Entry A) UpdateChunkPoolsCS
// -----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void UpdateChunkPoolsCS(uint3 tid : SV_DispatchThreadID)
{
	uint dim = g_CB.ChunkVisibleDim;
	if (tid.x >= dim || tid.y >= dim)
		return;

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

// -----------------------------------------------------------------------------
// Entry B) FillNewPoolsCS
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void FillNewPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
	uint dim = g_CB.ChunkVisibleDim;
	uint visibleCells = dim * dim;

	uint cellIndex = gid.x;
	if (cellIndex >= visibleCells)
		return;

	uint pool = cellIndex;

	PoolDirty pd = g_PoolDirty[pool];
	if (pd.Dirty != 1u)
		return;

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
		uint base0 = pool * g_CB.SamplesPerChunk;
		for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
		{
			g_PoolPositions[base0 + s].Position = float3(0.0f, INVALID_NAN, 0.0f);
			g_PoolPositions[base0 + s].Seed = 0u;
		}

		GroupMemoryBarrierWithGroupSync();
		if (tid.x == 0u)
		{
			pd.Dirty = 0u;
			g_PoolDirty[pool] = pd;
		}
		return;
	}

	float chunkHeight = SampleWorldHeightAtWorldXZLevel(chunkOriginXZ, 5.0f);
	if (tid.x == 0u)
	{
		PoolChunkCoord pc = g_PoolChunkCoord[pool];
		pc.ChunkCoord = chunkCoord;
		pc.ChunkHeight = chunkHeight;
		g_PoolChunkCoord[pool] = pc;
	}

	uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);
	uint base = pool * g_CB.SamplesPerChunk;

	for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
	{
		uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

		float ux = Rand01(seed ^ 0x2222u);
		float uz = Rand01(seed ^ 0x3333u);

		float jx = (Rand01(seed ^ 0x4444u) - 0.5f) * g_CB.Jitter;
		float jz = (Rand01(seed ^ 0x5555u) - 0.5f) * g_CB.Jitter;

		float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * g_CB.ChunkSize;
		float2 posXZ = chunkOriginXZ + localXZ;

		float y = SampleTerrainSurfaceHeightAtWorldXZ(posXZ) + g_CB.YOffset;

		g_PoolPositions[base + s].Position = float3(posXZ.x, y, posXZ.y);
		g_PoolPositions[base + s].Seed = seed;
	}

	GroupMemoryBarrierWithGroupSync();
	if (tid.x == 0u)
	{
		pd.Dirty = 0u;
		g_PoolDirty[pool] = pd;
	}
}

// -----------------------------------------------------------------------------
// Entry B-1) ClearSpeciesCountersCS
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void ClearSpeciesCountersCS(uint3 tid : SV_DispatchThreadID)
{
	uint n = MAX_GRASS_SPECIES * 3u;
	if (tid.x >= n)
		return;

	g_SpeciesLodCounts[tid.x] = 0u;
	g_SpeciesLodOffsets[tid.x] = 0u;
	g_SpeciesLodWriteCounters[tid.x] = 0u;
}

// -----------------------------------------------------------------------------
// Entry B-2) CountInstancesFromPoolsCS
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void CountInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
	uint cellIndex = gid.x;

	ChunkContext ctx;
	if (!BuildChunkContext(cellIndex, ctx))
		return;

	uint pool = cellIndex;
	uint base = pool * g_CB.SamplesPerChunk;

	for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
	{
		uint typeId, lodIndex;
		float press01, scale, yaw, pitch, bend01;
		uint seed8, variantId, atlasIndex;
		float3 posWS;

		if (!EvaluateSpawn(ctx, base, s,
			typeId, lodIndex,
			press01, scale, yaw, pitch, bend01,
			seed8, variantId, atlasIndex,
			posWS))
			continue;

		uint idx = typeId * 3u + lodIndex;
		InterlockedAdd(g_SpeciesLodCounts[idx], 1u);
	}
}

// -----------------------------------------------------------------------------
// Entry B-3) PrefixSpeciesOffsetsCS
// -----------------------------------------------------------------------------
[numthreads(1, 1, 1)]
void PrefixSpeciesOffsetsCS(uint3 tid : SV_DispatchThreadID)
{
	uint numTypes = min(max(g_CB.NumGrassTypes, 1u), (uint) MAX_GRASS_SPECIES);

	uint running0 = 0u;
	uint running1 = 0u;
	uint running2 = 0u;

	for (uint t = 0u; t < numTypes; ++t)
	{
		uint i0 = t * 3u + 0u;
		uint i1 = t * 3u + 1u;
		uint i2 = t * 3u + 2u;

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

// -----------------------------------------------------------------------------
// Entry C) BuildInstancesFromPoolsCS
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void BuildInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
	uint cellIndex = gid.x;

	ChunkContext ctx;
	if (!BuildChunkContext(cellIndex, ctx))
		return;

	uint pool = cellIndex;
	uint base = pool * g_CB.SamplesPerChunk;

	for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
	{
		uint typeId, lodIndex;
		float press01, scale, yaw, pitch, bend01;
		uint seed8, variantId, atlasIndex;
		float3 posWS;

		if (!EvaluateSpawn(ctx, base, s,
			typeId, lodIndex,
			press01, scale, yaw, pitch, bend01,
			seed8, variantId, atlasIndex,
			posWS))
			continue;

		uint meshId =
			(lodIndex == 0u) ? g_SpeciesLOD0MeshId[typeId] :
			(lodIndex == 1u) ? g_SpeciesLOD1MeshId[typeId] :
							   g_SpeciesLOD2MeshId[typeId];

		WaveReserveMeshCounter_Grouped(meshId);

		uint idx = typeId * 3u + lodIndex;

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