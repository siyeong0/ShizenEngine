// ============================================================================
// GrassGenerateInstances.hlsl
// - A style: Base Grass carpet + Flower overlay clusters
//   * Base grass species: [0 .. NumBaseGrassSpecies-1] (default 16)
//   * Flower species:     [NumBaseGrassSpecies .. NumSpecies-1]
// - For each candidate point:
//   * Spawn grass (carpet) from grass species weights (NO cluster mask)
//   * Spawn flower (overlay) based on macro-cell flower selection + organic cluster mask
// - Flower areas still keep grass because grass pass is independent.
// - Fix: Voronoi/Worley integer cell handling for negative coordinates (NO uint-cast)
// - BlueNoise threshold generator (StablePointThreshold01) used
// - FillNewPoolsCS uses stratified grid + small jitter (already in your code)
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

// Species selection tables (inclusive prefix)
// - For grass: this buffer stores prefix for species[0..NumBaseGrassSpecies-1]
StructuredBuffer<float> g_SpeciesWeightPrefix;

// Flower selection table (inclusive prefix) for flowers only
// - size >= NumFlowers (NumSpecies - NumBaseGrassSpecies)
// - flowerSpecies = NumBaseGrassSpecies + selectedIndex
StructuredBuffer<float> g_FlowerWeightPrefix;

// Species variation tables
StructuredBuffer<uint> g_SpeciesVarOffsets; // size >= NumSpecies+1
StructuredBuffer<uint> g_SpeciesVarCounts; // size >= NumSpecies
StructuredBuffer<uint> g_SpeciesVarToTypeId; // flat index -> typeId

// Per-type params (minScale,maxScale,bendMin,bendMax)
StructuredBuffer<float4> g_TypeParams0;

// Per-species clustering params
// float4(ClusterStrength, ClusterScaleMeters, ClusterJitter01, Reserved)
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

// "Macro Voronoi cell" world scale in meters (controls big region size)
static const float VORONOI_MACRO_CELL_METERS = 24.0f;

// For flowers: inside cluster area slightly denser than base flower probability
static const float FLOWER_CLUSTER_BOOST = 1.35f;

#ifndef MAX_CELL_CLUSTERS
#define MAX_CELL_CLUSTERS 4u
#endif

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

// IMPORTANT: keep int2 -> hash stable for negatives
uint Hash2i(int2 v, uint salt)
{
	uint x = asuint(v.x);
	uint y = asuint(v.y);
	uint h = (x * 73856093u) ^ (y * 19349663u) ^ salt;
	return WangHash(h);
}

uint Hash2u(uint2 p)
{
	uint h = p.x * 1664525u + 1013904223u;
	h ^= p.y * 22695477u + 1u;
	h = WangHash(h);
	return h;
}

float Hash01_u2(uint2 p)
{
	return (Hash2u(p) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float2 Hash02_u2(uint2 p)
{
	uint h0 = Hash2u(p);
	uint h1 = WangHash(h0 ^ 0x9E3779B9u);
	float a = (h0 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	float b = (h1 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	return float2(a, b);
}

float2 Hash02_i2(int2 cell, uint seed)
{
	uint h = Hash2i(cell, seed);
	uint h1 = WangHash(h ^ 0x9E3779B9u);
	float a = (h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	float b = (h1 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	return float2(a, b);
}

float2 Smooth2(float2 t)
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// -----------------------------------------------------------------------------
// Rotation helper
// -----------------------------------------------------------------------------
float2 Rotate2D(float2 v, float a)
{
	float s = sin(a);
	float c = cos(a);
	return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// -----------------------------------------------------------------------------
// Tiny procedural value noise (ONLY for mild warp)
// -----------------------------------------------------------------------------
float ValueNoise2D(float2 x, uint seed)
{
	float2 p = floor(x);
	float2 f = frac(x);

	int2 i00 = int2(p) + int2(0, 0);
	int2 i10 = int2(p) + int2(1, 0);
	int2 i01 = int2(p) + int2(0, 1);
	int2 i11 = int2(p) + int2(1, 1);

	uint h00 = Hash2i(i00, seed);
	uint h10 = Hash2i(i10, seed);
	uint h01 = Hash2i(i01, seed);
	uint h11 = Hash2i(i11, seed);

	float v00 = (h00 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	float v10 = (h10 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	float v01 = (h01 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
	float v11 = (h11 & 0x00FFFFFFu) * (1.0f / 16777216.0f);

	float2 u = Smooth2(f);
	float a = lerp(v00, v10, u.x);
	float b = lerp(v01, v11, u.x);
	return lerp(a, b, u.y);
}

// -----------------------------------------------------------------------------
// Isotropic / decorrelated domain warp (small)
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
	int2 SeedCell; // SIGNED
	float2 SeedPos;
};

WorleyResult WorleyF1F2_WithSeed(float2 x, uint seed)
{
	float2 pF = floor(x);
	float2 f = frac(x);

	int2 p = int2(pF);

	float best1 = 1e9f;
	float best2 = 1e9f;

	int2 bestCell = int2(0, 0);
	float2 bestPos = 0.0f;

	[unroll]
	for (int j = -1; j <= 1; ++j)
	{
		[unroll]
		for (int i = -1; i <= 1; ++i)
		{
			int2 cell = p + int2(i, j);

			float2 rnd = Hash02_i2(cell, seed);
			float2 feature = float2(i, j) + rnd;
			float2 q = feature - f;

			float d2 = dot(q, q);

			if (d2 < best1)
			{
				best2 = best1;
				best1 = d2;
				bestCell = cell;
				bestPos = float2(cell) + rnd;
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
	uint w = (word == 0u) ? b.M.x : (word == 1u) ? b.M.y : (word == 2u) ? b.M.z : b.M.w;
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
// Variation mapping
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
// BlueNoise threshold generator (stable, no axis bias)
// -----------------------------------------------------------------------------
float StablePointThreshold01(float2 worldXZ, uint seed)
{
	const float blueFreq = 0.85f;

	uint h = WangHash(seed ^ 0xA53A9E37u);
	float2 off = float2(
		((h >> 0) & 1023u) * (1.0f / 1024.0f),
		((h >> 10) & 1023u) * (1.0f / 1024.0f));

	uint mode = (h >> 24) & 3u;

	float2 uv = frac(worldXZ * blueFreq + off);

	if (mode == 1u)
		uv = float2(uv.y, uv.x);
	else if (mode == 2u)
		uv = float2(1.0f - uv.x, uv.y);
	else if (mode == 3u)
		uv = float2(uv.x, 1.0f - uv.y);

	float bn = g_BlueNoiseTex.SampleLevel(g_LinearWrapSampler, uv, 0.0f).r;

	float sh = Rand01(h ^ 0x1BADC0DEu);
	return frac(bn + sh * (1.0f / 256.0f));
}

// -----------------------------------------------------------------------------
// Shared chunk context
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
// Species selection helpers
// -----------------------------------------------------------------------------
uint PickGrassSpecies(uint seed)
{
	uint nGrass = min(max(g_CB.NumBaseGrassSpecies, 1u), g_CB.NumSpecies);
	if (nGrass == 1u)
		return 0u;

	float total = max(g_SpeciesWeightPrefix[nGrass - 1u], 0.0f);
	if (total <= 1e-8f)
		return WangHash(seed ^ 0x13579BDFu) % nGrass;

	float r = Rand01(seed ^ 0x2468ACE0u) * total;

	[loop]
	for (uint s = 0u; s < MAX_GRASS_SPECIES; ++s)
	{
		if (s >= nGrass)
			break;

		if (r <= g_SpeciesWeightPrefix[s])
			return s;
	}
	return nGrass - 1u;
}

uint PickFlowerSpeciesFromCell(uint cellHash)
{
	uint base = min(g_CB.NumBaseGrassSpecies, g_CB.NumSpecies);
	uint num = g_CB.NumSpecies;
	if (num <= base)
		return 0u;

	uint numFlowers = num - base;
	if (numFlowers == 1u)
		return base;

	float total = max(g_FlowerWeightPrefix[numFlowers - 1u], 0.0f);
	if (total <= 1e-8f)
		return base + (WangHash(cellHash) % numFlowers);

	float r = Rand01(cellHash ^ 0x13579BDFu) * total;

	[loop]
	for (uint i = 0u; i < MAX_GRASS_SPECIES; ++i)
	{
		if (i >= numFlowers)
			break;

		if (r <= g_FlowerWeightPrefix[i])
			return base + i;
	}

	return num - 1u;
}

// -----------------------------------------------------------------------------
// Organic blob helpers
// -----------------------------------------------------------------------------
float SmoothMin(float a, float b, float k)
{
	float h = saturate(0.5f + 0.5f * (b - a) / max(k, 1e-6f));
	return lerp(b, a, h) - k * h * (1.0f - h);
}

float SdEllipseRot(float2 p, float2 ab, float ang)
{
	float2 q = Rotate2D(p, ang);
	float2 k = q / max(ab, 1e-6f.xx);
	return length(k) - 1.0f;
}

float SdfToMask(float d, float softness)
{
	return 1.0f - smoothstep(-softness, +softness, d);
}

struct FlowerClusterOut
{
	uint FlowerSpecies;
	float ClusterMask01;
	float ClusterSelect01;
};

FlowerClusterOut EvaluateFlowerClusters(float2 worldXZ)
{
	FlowerClusterOut o;
	o.FlowerSpecies = 0u;
	o.ClusterMask01 = 0.0f;
	o.ClusterSelect01 = 0.0f;

	uint base = min(g_CB.NumBaseGrassSpecies, g_CB.NumSpecies);
	if (g_CB.NumSpecies <= base)
		return o;

	uint vorSeed = WangHash(g_CB.SeedSalt ^ 0xCAFEBABEu);

	float macroScale = max(VORONOI_MACRO_CELL_METERS, 1e-3f);
	float2 x_macro = worldXZ / macroScale;

	WorleyResult wr = WorleyF1F2_WithSeed(x_macro, vorSeed);

	uint cellHash = Hash2i(wr.SeedCell, vorSeed ^ 0x31415926u);

	uint flowerSp = PickFlowerSpeciesFromCell(cellHash);
	o.FlowerSpecies = flowerSp;

	float4 cp = g_SpeciesClusterParams[flowerSp];
	float strength = saturate(cp.x);
	float clusterScaleM = max(cp.y, 0.01f);
	float jitter01 = saturate(cp.z);

	// If cluster disabled, treat as no flower clusters
	if (strength <= 1e-5f)
		return o;

	float2 local = (x_macro - wr.SeedPos);

	uint warpSeed = WangHash(cellHash ^ 0x9E3779B9u);
	float baseAng = Rand01(warpSeed ^ 0xABCDu) * GRASS_TWO_PI;
	local = Rotate2D(local, baseAng);

	float warpAmp = jitter01 * lerp(0.10f, 0.28f, strength);
	local = DomainWarp2D_Isotropic(local, warpAmp, warpSeed);

	float ratio = clusterScaleM / macroScale;
	uint clusterCount =
		(ratio < 0.18f) ? 4u :
		(ratio < 0.30f) ? 3u :
		(ratio < 0.55f) ? 2u : 1u;

	{
		float r = Rand01(cellHash ^ 0xC001D00Du);
		if (clusterCount < MAX_CELL_CLUSTERS && r < saturate((strength - 0.35f) * 0.65f))
			clusterCount = min(clusterCount + 1u, MAX_CELL_CLUSTERS);
		clusterCount = max(1u, min(clusterCount, MAX_CELL_CLUSTERS));
	}

	float baseR = clamp(clusterScaleM / macroScale, 0.08f, 0.80f);

	float soft = lerp(0.22f, 0.10f, strength) * max(baseR, 0.12f);
	float kSmooth = lerp(0.08f, 0.03f, strength);

	float dUnion = 1e9f;

	[unroll]
	for (uint i = 0u; i < MAX_CELL_CLUSTERS; ++i)
	{
		if (i >= clusterCount)
			break;

		uint si = WangHash(cellHash ^ (0xBADC0FFEu + 17u * i));

		float2 off = Hash02_i2(wr.SeedCell + int2((int) (i * 31u), (int) (i * 57u)), si) * 2.0f - 1.0f;
		off *= lerp(0.10f, 0.65f, jitter01);

		float blobAng = Rand01(si ^ 0x2468ACE0u) * GRASS_TWO_PI;
		float ar = lerp(0.65f, 1.45f, Rand01(si ^ 0x11112222u));
		float rr = baseR * lerp(0.80f, 1.20f, Rand01(si ^ 0x33334444u));

		float2 ab = float2(rr * ar, rr / max(ar, 1e-3f));

		float2 p = local - off;
		p = DomainWarp2D_Isotropic(p, warpAmp * 0.55f, si ^ 0xFACECAFEu);

		float d = SdEllipseRot(p, ab, blobAng);
		dUnion = SmoothMin(dUnion, d, kSmooth);
	}

	float mask = SdfToMask(dUnion, soft);
	float k = lerp(1.0f, 2.8f, strength);
	mask = saturate(pow(saturate(mask), k));

	o.ClusterMask01 = mask;
	o.ClusterSelect01 = saturate(mask * strength);
	return o;
}

// -----------------------------------------------------------------------------
// Spawn output
// -----------------------------------------------------------------------------
struct SpawnOut
{
	uint TypeId;
	uint LodIndex;
	float Press01;
	float Scale;
	float Yaw;
	float Pitch;
	float Bend01;
	uint Seed8;
	uint VariantId;
	uint AtlasIndex;
	float3 PosWS;
};

// -----------------------------------------------------------------------------
// Spawn attempt: Grass (carpet)
// -----------------------------------------------------------------------------
bool TrySpawnGrass(ChunkContext ctx, uint poolBase, uint sampleIndex, out SpawnOut o)
{
	o.TypeId = 0u;
	o.LodIndex = 0u;
	o.Press01 = 0.0f;
	o.Scale = 0.0f;
	o.Yaw = 0.0f;
	o.Pitch = 0.0f;
	o.Bend01 = 0.0f;
	o.Seed8 = 0u;
	o.VariantId = 0u;
	o.AtlasIndex = 0u;
	o.PosWS = float3(0, 0, 0);

	PoolEntry p = g_PoolPositions[poolBase + sampleIndex];
	if (isnan(p.Position.y))
	{
		o.PosWS = float3(0.0f, INVALID_NAN, 0.0f);
		return false;
	}

	float2 posXZ = p.Position.xz;

	float D = SampleWorldDensity(posXZ, INSTANCE_DENSITY_MIP);
	if (D <= DENSITY_DISABLE_THRESHOLD)
		return false;

	uint seed = WangHash(p.Seed ^ (sampleIndex * 0x9E3779B9u));

	// Carpet probability: just density
	float prob = saturate(D);
	if (prob <= 0.001f)
		return false;

	float th = StablePointThreshold01(posXZ, seed ^ 0xC0FFEEu);
	if (th > prob)
		return false;

	// Pick grass species from [0..NumBaseGrassSpecies-1]
	uint grassSp = PickGrassSpecies(seed ^ 0x51EC7EEDu);

	uint varId = PickVariationUniform(grassSp, seed ^ 0xBBBBu);
	uint typeId = MapSpeciesVariationToTypeId(grassSp, varId);

	if (typeId >= g_CB.NumGrassTypes)
		typeId = typeId % max(g_CB.NumGrassTypes, 1u);

	float4 typeP = g_TypeParams0[typeId];
	float minScale = typeP.x;
	float maxScale = max(typeP.y, minScale);
	float bendMin = typeP.z;
	float bendMax = max(typeP.w, bendMin);

	float press01 = saturate(SampleInteraction(posXZ));
	float3 posWS = p.Position;

	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 dxz = posWS.xz - camXZ;
	float distSqr = dot(dxz, dxz);

	uint lodIndex =
		(distSqr < ctx.Lod0Sqr) ? 0u :
		(distSqr < ctx.Lod1Sqr) ? 1u : 2u;

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

	o.TypeId = typeId;
	o.LodIndex = lodIndex;
	o.Press01 = press01;
	o.Scale = scale;
	o.Yaw = yaw;
	o.Pitch = pitch;
	o.Bend01 = bend01;
	o.Seed8 = seed8;
	o.VariantId = variantId;
	o.AtlasIndex = atlasIndex;
	o.PosWS = posWS;
	return true;
}

// -----------------------------------------------------------------------------
// Spawn attempt: Flower (overlay clusters)
// -----------------------------------------------------------------------------
bool TrySpawnFlower(ChunkContext ctx, uint poolBase, uint sampleIndex, out SpawnOut o)
{
	o.TypeId = 0u;
	o.LodIndex = 0u;
	o.Press01 = 0.0f;
	o.Scale = 0.0f;
	o.Yaw = 0.0f;
	o.Pitch = 0.0f;
	o.Bend01 = 0.0f;
	o.Seed8 = 0u;
	o.VariantId = 0u;
	o.AtlasIndex = 0u;
	o.PosWS = float3(0, 0, 0);

	PoolEntry p = g_PoolPositions[poolBase + sampleIndex];
	if (isnan(p.Position.y))
	{
		o.PosWS = float3(0.0f, INVALID_NAN, 0.0f);
		return false;
	}

	uint base = min(g_CB.NumBaseGrassSpecies, g_CB.NumSpecies);
	if (g_CB.NumSpecies <= base)
		return false;

	float2 posXZ = p.Position.xz;

	float D = SampleWorldDensity(posXZ, INSTANCE_DENSITY_MIP);
	if (D <= DENSITY_DISABLE_THRESHOLD)
		return false;

	uint seed = WangHash(p.Seed ^ (sampleIndex * 0x9E3779B9u) ^ 0xF10A3C01u);

	// Evaluate flower clusters
	FlowerClusterOut cc = EvaluateFlowerClusters(posXZ);
	if (cc.FlowerSpecies < base)
		return false;

	// Must be inside cluster mask (soft), but allow tiny sprinkle near edges
	float mask = cc.ClusterMask01; // 0..1
	float shaped = saturate(pow(mask, 0.85f));

	// Global flower scale (tuning) + cluster boost
	float prob = D * saturate(g_CB.FlowerDensityScale);
	prob *= lerp(0.10f, 1.0f, shaped);
	prob *= lerp(1.0f, FLOWER_CLUSTER_BOOST, shaped);
	prob = saturate(prob);

	if (prob <= 0.001f)
		return false;

	float th = StablePointThreshold01(posXZ, seed ^ 0xABCD1234u);
	if (th > prob)
		return false;

	uint flowerSp = cc.FlowerSpecies;

	uint varId = PickVariationUniform(flowerSp, seed ^ 0xB00B1Eu);
	uint typeId = MapSpeciesVariationToTypeId(flowerSp, varId);

	if (typeId >= g_CB.NumGrassTypes)
		typeId = typeId % max(g_CB.NumGrassTypes, 1u);

	float4 typeP = g_TypeParams0[typeId];
	float minScale = typeP.x;
	float maxScale = max(typeP.y, minScale);
	float bendMin = typeP.z;
	float bendMax = max(typeP.w, bendMin);

	float press01 = saturate(SampleInteraction(posXZ));
	float3 posWS = p.Position;

	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 dxz = posWS.xz - camXZ;
	float distSqr = dot(dxz, dxz);

	uint lodIndex =
		(distSqr < ctx.Lod0Sqr) ? 0u :
		(distSqr < ctx.Lod1Sqr) ? 1u : 2u;

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

	o.TypeId = typeId;
	o.LodIndex = lodIndex;
	o.Press01 = press01;
	o.Scale = scale;
	o.Yaw = yaw;
	o.Pitch = pitch;
	o.Bend01 = bend01;
	o.Seed8 = seed8;
	o.VariantId = variantId;
	o.AtlasIndex = atlasIndex;
	o.PosWS = posWS;
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
// Entry B) FillNewPoolsCS (stratified grid + small jitter)
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

		float fN = (float) max(g_CB.SamplesPerChunk, 1u);
		uint gridDim = (uint) ceil(sqrt(fN));
		gridDim = max(gridDim, 1u);

		uint ix = (gridDim > 0u) ? (s % gridDim) : 0u;
		uint iz = (gridDim > 0u) ? (s / gridDim) : 0u;

		float2 cell01 = (float2(ix, iz) + 0.5f.xx) / (float) gridDim;

		float2 j01 = (Hash02_u2(uint2(ix, iz) ^ uint2(chunkSeed, seed)) - 0.5f.xx);
		float jitterCell = saturate(g_CB.Jitter) * 0.35f;

		cell01 += j01 * (jitterCell / (float) gridDim);
		cell01 = saturate(cell01);

		float2 localXZ = cell01 * g_CB.ChunkSize;
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
// Entry B-2) CountInstancesFromPoolsCS (grass + flower)
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
		SpawnOut so;

		// 1) Grass
		if (TrySpawnGrass(ctx, base, s, so))
		{
			uint idx = so.TypeId * 3u + so.LodIndex;
			InterlockedAdd(g_SpeciesLodCounts[idx], 1u);
		}

		// 2) Flower
		if (TrySpawnFlower(ctx, base, s, so))
		{
			uint idx = so.TypeId * 3u + so.LodIndex;
			InterlockedAdd(g_SpeciesLodCounts[idx], 1u);
		}
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
// Entry C) BuildInstancesFromPoolsCS (grass + flower)
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
		SpawnOut so;

		// ---- helper lambda-like macro (manual) ----
		// Build function body duplicated for grass/flower for speed + clarity.
		// ------------------------------------------

		// 1) Grass
		if (TrySpawnGrass(ctx, base, s, so))
		{
			uint meshId =
				(so.LodIndex == 0u) ? g_SpeciesLOD0MeshId[so.TypeId] :
				(so.LodIndex == 1u) ? g_SpeciesLOD1MeshId[so.TypeId] :
									  g_SpeciesLOD2MeshId[so.TypeId];

			WaveReserveMeshCounter_Grouped(meshId);

			uint idx = so.TypeId * 3u + so.LodIndex;

			uint local = 0u;
			InterlockedAdd(g_SpeciesLodWriteCounters[idx], 1u, local);

			uint globalIndex = g_SpeciesLodOffsets[idx] + local;

			if (so.LodIndex == 0u)
			{
				GrassMeshInstance inst = MakeGrassMeshInstance(so.PosWS, so.Scale, so.Yaw, so.Pitch, so.Bend01, so.Press01, so.VariantId, so.Seed8);
				g_OutInstancesLOD0[globalIndex] = inst;
			}
			else if (so.LodIndex == 1u)
			{
				GrassCrossPlaneInstance inst = MakeGrassCrossPlaneInstance(so.PosWS, so.Scale, so.Yaw, so.Bend01, so.Press01, so.VariantId, so.Seed8, 0u, 0u);
				g_OutInstancesLOD1[globalIndex] = inst;
			}
			else
			{
				GrassBillboardInstance inst = MakeGrassBillboardInstance(so.PosWS, so.Scale, so.Yaw, so.AtlasIndex, so.Seed8);
				g_OutInstancesLOD2[globalIndex] = inst;
			}
		}

		// 2) Flower
		if (TrySpawnFlower(ctx, base, s, so))
		{
			uint meshId =
				(so.LodIndex == 0u) ? g_SpeciesLOD0MeshId[so.TypeId] :
				(so.LodIndex == 1u) ? g_SpeciesLOD1MeshId[so.TypeId] :
									  g_SpeciesLOD2MeshId[so.TypeId];

			WaveReserveMeshCounter_Grouped(meshId);

			uint idx = so.TypeId * 3u + so.LodIndex;

			uint local = 0u;
			InterlockedAdd(g_SpeciesLodWriteCounters[idx], 1u, local);

			uint globalIndex = g_SpeciesLodOffsets[idx] + local;

			if (so.LodIndex == 0u)
			{
				GrassMeshInstance inst = MakeGrassMeshInstance(so.PosWS, so.Scale, so.Yaw, so.Pitch, so.Bend01, so.Press01, so.VariantId, so.Seed8);
				g_OutInstancesLOD0[globalIndex] = inst;
			}
			else if (so.LodIndex == 1u)
			{
				GrassCrossPlaneInstance inst = MakeGrassCrossPlaneInstance(so.PosWS, so.Scale, so.Yaw, so.Bend01, so.Press01, so.VariantId, so.Seed8, 0u, 0u);
				g_OutInstancesLOD1[globalIndex] = inst;
			}
			else
			{
				GrassBillboardInstance inst = MakeGrassBillboardInstance(so.PosWS, so.Scale, so.Yaw, so.AtlasIndex, so.Seed8);
				g_OutInstancesLOD2[globalIndex] = inst;
			}
		}
	}
}