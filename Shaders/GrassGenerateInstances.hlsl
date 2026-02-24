// ============================================================================
// GrassGenerateInstances.hlsl
// - ¡°Patch mask + point sampling¡± (canonical)
// - ROUND patches: Worley(Cellular) distance field (no noise textures)
// - Priority + exclusion workflow:
//     1) Special patch winner-takes-all (+ boundary gap)
//     2) Try spawn special
//     3) Else spawn base
//     4) Base density is suppressed inside special patches
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
StructuredBuffer<float> g_SpeciesWeightPrefix; // prefix sum of Weight (size >= NumSpecies)

// Species variation tables
StructuredBuffer<uint> g_SpeciesVarOffsets; // size >= NumSpecies+1
StructuredBuffer<uint> g_SpeciesVarCounts; // size >= NumSpecies
StructuredBuffer<uint> g_SpeciesVarToTypeId; // flat index -> typeId

// Per-type params (minScale,maxScale,bendMin,bendMax)
StructuredBuffer<float4> g_TypeParams0;

// Per-species clustering params
// float4(ClusterStrength, ClusterScaleMeters, ClusterJitter01, BaseSuppressInPatch01)
StructuredBuffer<float4> g_SpeciesClusterParams;

// NEW: per-species flags
// bit0: IsSpecial (macro patch species)
StructuredBuffer<uint> g_SpeciesFlags;

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
static const uint INVALID_U32 = 0xFFFFFFFFu;
static const float INVALID_NAN = asfloat(0x7FC00000u);

static const float CHUNK_DENSITY_MIP = 1.0f;
static const float INSTANCE_DENSITY_MIP = 0.0f;
static const float DENSITY_DISABLE_THRESHOLD = 0.01f;

static const float CHUNK_AABB_HALF_Y = 20.0f;

// Special patch winner exclusion tuning
static const float SP_MIN_BEST_SCORE = 1e-5f;
static const float SP_GAP_ABS_MIN = 0.015f;
static const float SP_GAP_REL = 0.20f;

// Special patch mask threshold (must be inside patch enough)
static const float SP_MIN_MASK_TO_SPAWN = 0.10f;

// Base selection exclusion tuning (optional, lighter)
static const float BS_MIN_BEST_SCORE = 1e-6f;

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
    // smootherstep
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
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

float2 DomainWarp2D(float2 x, float warpAmp, uint seed)
{
	if (warpAmp <= 0.0f)
		return x;

    // VERY low freq to avoid grid-looking artifacts
	float wx = ValueNoise2D(x * 0.21f, seed ^ 0x1111u);
	float wz = ValueNoise2D(x * 0.21f + 13.37f, seed ^ 0x2222u);

	float2 w = (float2(wx, wz) * 2.0f - 1.0f) * warpAmp;
	return x + w;
}

// -----------------------------------------------------------------------------
// WORLEY(Cellular) distance field for round patches
// -----------------------------------------------------------------------------
float WorleyF1(float2 x, uint seed)
{
	float2 p = floor(x);
	float2 f = frac(x);

	float best = 1e9f;

    [unroll]
	for (int j = -1; j <= 1; ++j)
	{
        [unroll]
		for (int i = -1; i <= 1; ++i)
		{
			uint2 cell = (uint2) (p + float2(i, j));

            // per-cell random point in cell
			float2 rnd = Hash02(cell ^ uint2(seed, seed * 1664525u));
			float2 q = float2(i, j) + rnd - f;

			float d2 = dot(q, q);
			best = min(best, d2);
		}
	}

	return sqrt(best);
}

float WorleyRoundField01(float2 x, uint seed)
{
	float f1a = WorleyF1(x, seed ^ 0xA53u);

    // rotate & offset a bit (helps break repetition)
	const float2x2 R = float2x2(0.80901699f, -0.58778525f,
                               0.58778525f, 0.80901699f);
	float2 xr = mul(R, x + 7.13f);
	float f1b = WorleyF1(xr, seed ^ 0xB71u);

    // combine
	float f = min(f1a, f1b * 1.05f);

    // normalize-ish
	return saturate(f * 1.25f);
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
// Species weights from prefix table
// -----------------------------------------------------------------------------
float GetSpeciesWeight(uint s)
{
	if (s == 0u)
		return max(g_SpeciesWeightPrefix[0], 0.0f);
	float a = g_SpeciesWeightPrefix[s - 1u];
	float b = g_SpeciesWeightPrefix[s];
	return max(b - a, 0.0f);
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
// PATCH FIELD (ROUND): Worley distance -> patch mask
// -----------------------------------------------------------------------------
float ComputePatchMask01(uint speciesId, float2 worldXZ)
{
	float4 cp = g_SpeciesClusterParams[speciesId];

	float strength = saturate(cp.x);
	float scaleM = max(cp.y, 0.001f); // patch size in meters
	float jitter01 = saturate(cp.z);

	if (strength <= 1e-5f)
		return 1.0f;

    // noise space where 1 cell ~= 1 patch unit
	float2 x = worldXZ / scaleM;

    // species-stable seed
	uint spSeed = WangHash((speciesId + 1u) ^ g_CB.SeedSalt ^ 0xC1D2E3F4u);

    // domain warp in noise space
	float warpAmp = jitter01 * 0.45f;
	x = DomainWarp2D(x, warpAmp, spSeed);

	float F = WorleyRoundField01(x, spSeed); // 0..1

	float radius = lerp(0.62f, 0.40f, strength);
	float softness = lerp(0.18f, 0.08f, strength);

	float M = 1.0f - smoothstep(radius - softness, radius + softness, F);

	float k = lerp(1.0f, 2.2f, strength);
	M = saturate(pow(M, k));

	return M;
}

// -----------------------------------------------------------------------------
// ¡°Point sampling¡± threshold generator (stable, blue-noise-ish)
// -----------------------------------------------------------------------------
float StablePointThreshold01(float2 worldXZ, uint seed)
{
	const float freq = 0.35f;

	float2 g = worldXZ * freq;
	int2 ig = int2(floor(g));

	uint2 cell = uint2(ig) ^ uint2(seed, seed * 1664525u);
	float base = Hash01(cell);

	float2 f = frac(g);
	float j = dot(f, float2(0.37f, 0.63f));
	return saturate(base * 0.85f + j * 0.15f);
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
// Special-first pattern
// -----------------------------------------------------------------------------
bool IsSpecialSpecies(uint s)
{
	return (g_SpeciesFlags[s] & 1u) != 0u;
}

// Returns:
//  - outSpecialId: chosen special species (if any)
//  - outBestScore/outSecondScore: scores in weight space
//  - outBestMask: patch mask of chosen winner
//  - outBaseScale: multiplicative scale for base density (<=1) due to all special patches
bool SelectSpecialPatchWinner(
    float2 worldXZ,
    uint seed,
    out uint outSpecialId,
    out float outBestScore,
    out float outSecondScore,
    out float outBestMask,
    out float outBaseScale
)
{
	outSpecialId = 0u;
	outBestScore = -1.0f;
	outSecondScore = -1.0f;
	outBestMask = 0.0f;
	outBaseScale = 1.0f;

	uint num = max(g_CB.NumSpecies, 1u);

    // totalW over ONLY specials (computed on the fly)
	float totalW = 0.0f;

	uint salt = WangHash(seed ^ 0x51515151u);

    [loop]
	for (uint s = 0u; s < MAX_GRASS_SPECIES; ++s)
	{
		if (s >= num)
			break;
		if (!IsSpecialSpecies(s))
			continue;

		float W = max(GetSpeciesWeight(s), 0.0f);
		if (W <= 0.0f)
			continue;

		totalW += W;

		float4 cp = g_SpeciesClusterParams[s];
		float strength = saturate(cp.x);

        // Special patch mask MUST be meaningful (even if strength small)
		float M = ComputePatchMask01(s, worldXZ);

        // score: weight * mask (patch membership)
        // (strength influences mask shape already; score uses M directly)
		float score = W * M;

        // tiny tie-break
		score += (Rand01(salt ^ (s * 0x9E3779B9u)) - 0.5f) * 1e-7f;

        // base density suppression field:
        // cp.w is BaseSuppressInPatch01: 0=no suppress, 1=full suppress at mask==1
		float suppress = saturate(cp.w);
		float scale = 1.0f - suppress * M; // inside patch => smaller
		outBaseScale = min(outBaseScale, scale);

		if (score > outBestScore)
		{
			outSecondScore = outBestScore;
			outBestScore = score;
			outSpecialId = s;
			outBestMask = M;
		}
		else if (score > outSecondScore)
		{
			outSecondScore = score;
		}
	}

    // If no specials found, just return false (but baseScale might remain 1)
	if (totalW <= 1e-8f)
		return false;

    // Need confident winner
	if (outBestScore < SP_MIN_BEST_SCORE)
		return false;

	float gap = max(SP_GAP_ABS_MIN, SP_GAP_REL * outBestScore);
	if ((outBestScore - max(outSecondScore, 0.0f)) < gap)
		return false;

    // Must be sufficiently inside patch, otherwise treat as outside
	if (outBestMask < SP_MIN_MASK_TO_SPAWN)
		return false;

	return true;
}

// Base winner: choose a base species (non-special) by max(weight)
// (you can extend later: multiply micro masks / biome constraints here)
bool SelectBaseWinner(
    float2 worldXZ,
    uint seed,
    out uint outBaseSpeciesId,
    out float outBestScore
)
{
	outBaseSpeciesId = 0u;
	outBestScore = -1.0f;

	uint num = max(g_CB.NumSpecies, 1u);

	uint salt = WangHash(seed ^ 0x61616161u);

    [loop]
	for (uint s = 0u; s < MAX_GRASS_SPECIES; ++s)
	{
		if (s >= num)
			break;
		if (IsSpecialSpecies(s))
			continue;

		float W = max(GetSpeciesWeight(s), 0.0f);
		if (W <= 0.0f)
			continue;

		float score = W;

        // tiny tie-break
		score += (Rand01(salt ^ (s * 0x9E3779B9u)) - 0.5f) * 1e-7f;

		if (score > outBestScore)
		{
			outBestScore = score;
			outBaseSpeciesId = s;
		}
	}

	return (outBestScore > BS_MIN_BEST_SCORE);
}

// -----------------------------------------------------------------------------
// EvaluateSpawn: special-first then base fill
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

    // --- Special-first selection ---
	uint spId;
	float spBest, spSecond, spMask, baseScale;
	bool hasSpecial = SelectSpecialPatchWinner(posXZ, seed, spId, spBest, spSecond, spMask, baseScale);

    // Base density suppression inside special patches (even if special fails to spawn)
	float baseD = D * saturate(baseScale);

    // Compute totalW across all species for normalization fallback (keeps stable)
	uint num = max(g_CB.NumSpecies, 1u);
	float totalW_all = g_SpeciesWeightPrefix[num - 1u];
	float invTotalW_all = (totalW_all > 1e-8f) ? rcp(totalW_all) : 0.0f;

    // Try spawn special if we have a confident patch winner
	uint chosenSpecies = INVALID_U32;

	if (hasSpecial)
	{
        // Special spawn probability:
        // - use (W*Mask)/totalW_all so global density remains stable-ish
		float spProb = saturate(D * spBest * invTotalW_all);

        // You can optionally boost special visibility a bit:
        // spProb = saturate(spProb * 1.15f);

		if (spProb > 0.001f)
		{
			float th = StablePointThreshold01(posXZ, seed ^ 0xBADC0DEu);
			if (th <= spProb)
			{
				chosenSpecies = spId;
			}
		}
	}

    // If no special instance, fill with base
	if (chosenSpecies == INVALID_U32)
	{
		uint baseId;
		float baseBest;
		if (!SelectBaseWinner(posXZ, seed, baseId, baseBest))
			return false;

        // Base spawn prob:
        // - use base density suppressed in patch zones
        // - normalized by totalW_all (simple, stable)
		float baseProb = saturate(baseD * baseBest * invTotalW_all);
		if (baseProb <= 0.001f)
			return false;

		float th = StablePointThreshold01(posXZ, seed ^ 0xC0FFEEu);
		if (th > baseProb)
			return false;

		chosenSpecies = baseId;
	}

    // Map species -> variation -> type
	uint varId = PickVariationUniform(chosenSpecies, seed ^ 0xBBBBu);
	uint typeId = MapSpeciesVariationToTypeId(chosenSpecies, varId);

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