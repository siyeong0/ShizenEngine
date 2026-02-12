// Shaders/GrassChunkPoolCS.hlsl
//
// FIX: Remove FreeList (race) by making poolIndex == cellIndex (stable mapping).
//
// Strategy:
// - UpdateChunkPoolsCS:
//   pool = cellIndex (no alloc/free). If desiredChunk changed -> mark pool dirty.
// - FillNewPoolsCS (only for dirty pools):
//   fill full SamplesPerChunk with VALID positions.
//   NOTE: PoolPositions.w is UNUSED (kept as 0) for future use.
// - BuildInstancesFromPoolsCS (every frame):
//   frustum cull by cached chunk height,
//   distance-based samplesThisChunk, per-chunk density tap,
//   per-sample thinning here only (height mask + interaction/press are sampled here).
//
// Assumptions:
// - NumPools == VisibleCells == ChunkVisibleDim * ChunkVisibleDim
// - poolIndex == cellIndex
//
// IMPORTANT (updated design):
// - g_MeshInstanceCountBuffer is the ONLY counter source.
//   * It acts as:
//     (A) append counter for instance buffers (returns base index)
//     (B) final per-mesh instanceCount used by WriteIndirectArgs.hlsl later
// - Per-slot counters are NOT written here anymore.
//   WriteIndirectArgs will map meshId -> slot drawCount (0/1) and args instanceCount.
//
// NOTE:
// - This file expects FrameConstants (g_FrameCB) contains CameraPosition and FrustumPlanesWS[6].
// - g_TerrainNormal is not used here.
// - PoolPositions stores float4(x, y, z, UNUSED). w is reserved for future use.
// - Out-of-heightfield cells write NaN y (rare), Build skips NaN.
//
// Internal optimizations applied:
// - Build: early random gate using (SpawnProb * chunkDensity) BEFORE expensive samples (height/interaction).
// - Build: avoid re-sampling height/interaction for rejected candidates.
// - Build: reduce per-iteration work by computing only what¡¯s needed before LOD split.
// - Wave reserve: still 3 meshes (LOD0/1/2), but only performs InterlockedAdd when a mesh has any lanes active.
// - Update: minimize redundant loads/stores.
// - Fill: write NaN-out cells with one tight loop; keep w always 0.

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
	// 0 = Clean, 1 = NeedsFill, 2 = Filling (optional safety)
	uint Dirty;
	uint3 _pad;
};

// ---------------------------------------------------------------------------
// UAVs / SRVs
// ---------------------------------------------------------------------------

RWStructuredBuffer<VisibleCell> g_VisibleCellTable;
RWStructuredBuffer<PoolChunkCoord> g_PoolChunkCoord;
RWStructuredBuffer<PoolDirty> g_PoolDirty;

RWStructuredBuffer<float4> g_PoolPositions; // [pool * SamplesPerChunk + i] = float4(x,y,z,UNUSED)

// Rendering outputs
RWStructuredBuffer<GrassMeshInstance>        g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance>  g_OutInstancesLOD2;

// Per-mesh instance counter (append counter + final instanceCount).
// Layout: uint counter per mesh at byteOffset = meshId * 4.
RWByteAddressBuffer g_MeshInstanceCountBuffer;

// Inputs
Texture2D<float> g_HeightField;
Texture2D<float> g_DensityField;
Texture2D<float> g_InteractionField;

// ---------------------------------------------------------------------------
// Constants / helpers
// ---------------------------------------------------------------------------

static const uint  INVALID_U32 = 0xFFFFFFFFu;
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

	// NOTE: mipLevel is intentionally used (coarse, stable density).
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
	float b = 1.0f - smoothstep(hMaxN - hFadeN, hMaxN, hMaxN);
	// NOTE: keep original intent: band-pass.
	b = 1.0f - smoothstep(hMaxN - hFadeN, hMaxN, hN);
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
		float  d = P.w;

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
// Distance-based sample scaling (used in Build)
// ---------------------------------------------------------------------------

static const float CHUNK_DENSITY_MIP          = 3.0f;
static const float DENSITY_DISABLE_THRESHOLD  = 0.01f;
static const uint  SAMPLES_MIN_FAR            = 16u;
static const uint  SAMPLES_MAX_CLAMP          = 4096u;

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
// Wave helpers (mesh counter reserve/scatter)
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
	uint bit  = lane & 31;
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
	uint cnt  = BallotCountBits(ballot);

	if (cnt != 0u && WaveIsFirstLane())
	{
		uint byteOffset = meshId * 4u;
		g_MeshInstanceCountBuffer.InterlockedAdd(byteOffset, cnt, base);
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
	return (WangHash(seed ^ 0xCAFEFu) >> 29) & 0x7u;
}

// ---------------------------------------------------------------------------
// Entry A) UpdateChunkPoolsCS  (poolIndex == cellIndex)
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
// Entry B) FillNewPoolsCS  (NO THINNING, w RESERVED)
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

		// NOTE: w is RESERVED (keep 0 for now).
		g_PoolPositions[base + s] = float4(posXZ.x, y, posXZ.y, 0.0f);
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
// - THINNING HERE
// - reserves output indices using g_MeshInstanceCountBuffer (meshId-based)
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

	// Apply per-chunk density once (cheap thinning control).
	uint densityScaled = (uint) round((float) samplesThisChunk * saturate(chunkDensity));
	samplesThisChunk = max(densityScaled, (uint) min(SAMPLES_MIN_FAR, samplesThisChunk));
	samplesThisChunk = min(samplesThisChunk, baseSamples);

	if (samplesThisChunk == 0u)
	{
		return;
	}

	// Mesh ids for each LOD (must be filled from CPU).
	// These are the IDs used by IndirectArgsSystem (mesh-based counting).
	uint meshId0 = g_CB.LOD0MeshId;
	uint meshId1 = g_CB.LOD1MeshId;
	uint meshId2 = g_CB.LOD2MeshId;

	uint base = pool * g_CB.SamplesPerChunk;
	uint chunkSeed = Hash2i(chunkCoord, g_CB.SeedSalt);

	// Precompute the cheapest part of the spawn gate (saves texture taps).
	float spawnGateBase = saturate(g_CB.SpawnProb * chunkDensity);

	for (uint s = tid.x; s < samplesThisChunk; s += 256u)
	{
		float4 p = g_PoolPositions[base + s];
		if (isnan(p.y))
		{
			continue;
		}

		uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

		// Early reject BEFORE expensive height/interaction samples.
		if (Rand01(seed ^ 0x4444u) > spawnGateBase)
		{
			continue;
		}

		float2 posXZ = float2(p.x, p.z);

		// Height band-pass (1 height tap).
		float hN = SampleHeightNormalized(posXZ);
		float heightMask = ComputeHeightMask(hN);
		if (heightMask <= 0.001f)
		{
			continue;
		}

		// Refine spawn gate with heightMask (still no interaction tap yet).
		float spawnGate = spawnGateBase * heightMask;
		if (spawnGate <= 0.001f)
		{
			continue;
		}

		// If you want pressed grass to reduce spawn, you need press01 (interaction tap).
		float press01 = saturate(SampleInteraction(posXZ));

		// Optional: pressed grass reduces density.
		// spawnGate *= (1.0f - 0.5f * press01);

		// Final gate with updated spawnGate.
		// NOTE: reuse a different salt to decorrelate.
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

		// Reserve per mesh (only InterlockedAdd when a mesh actually has active lanes).
		BallotMask b0 = WaveBallotWrap(emit0);
		BallotMask b1 = WaveBallotWrap(emit1);
		BallotMask b2 = WaveBallotWrap(emit2);

		uint base0 = WaveReserveMeshCounter(meshId0, b0);
		uint base1 = WaveReserveMeshCounter(meshId1, b1);
		uint base2 = WaveReserveMeshCounter(meshId2, b2);

		uint lane = WaveGetLaneIndex();

		if (emit0)
		{
			uint pfx = BallotPrefixCountBits(b0, lane);

			GrassMeshInstance inst = MakeGrassMeshInstance(posWS, scale, yaw, pitch, bend01, press01, variantId, seed8);
			g_OutInstancesLOD0[base0 + pfx] = inst;
		}
		else if (emit1)
		{
			uint pfx = BallotPrefixCountBits(b1, lane);

			GrassCrossPlaneInstance inst = MakeGrassCrossPlaneInstance(posWS, scale, yaw, bend01, press01, variantId, seed8, 0u, 0u);
			g_OutInstancesLOD1[base1 + pfx] = inst;
		}
		else
		{
			uint pfx = BallotPrefixCountBits(b2, lane);

			GrassBillboardInstance inst = MakeGrassBillboardInstance(posWS, scale, yaw, atlasIndex, seed8);
			g_OutInstancesLOD2[base2 + pfx] = inst;
		}
	}
}
