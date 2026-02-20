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

// -----------------------------------------------------------------------------
// UAVs / SRVs
// -----------------------------------------------------------------------------
RWStructuredBuffer<VisibleCell> g_VisibleCellTable;
RWStructuredBuffer<PoolChunkCoord> g_PoolChunkCoord;
RWStructuredBuffer<PoolDirty> g_PoolDirty;

// Pool entries
struct PoolEntry
{
	float3 Position;
	uint SpeciesId;
};
RWStructuredBuffer<PoolEntry> g_PoolPositions;

// Rendering outputs (packed per species/LOD via prefix sums)
RWStructuredBuffer<GrassMeshInstance> g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance> g_OutInstancesLOD2;

// Per-mesh counter (IndirectArgs mesh-based).
// Layout: uint counter per meshId at byteOffset = meshId*4.
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
Texture2D<float> g_InteractionField;

// -----------------------------------------------------------------------------
// Constants / helpers
// -----------------------------------------------------------------------------
static const uint INVALID_U32 = 0xFFFFFFFFu;
static const float INVALID_NAN = asfloat(0x7FC00000u);

// Density / sampling tuning
static const float CHUNK_DENSITY_MIP = 1.0f;
static const float INSTANCE_DENSITY_MIP = 0.0f;
static const float DENSITY_DISABLE_THRESHOLD = 0.01f;

// Height bounds for conservative chunk AABB (tune to your world)
static const float CHUNK_AABB_HALF_Y = 20.0f;

// -----------------------------------------------------------------------------
// Random / Hash (deterministic)
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
	{
		return false;
	}

	chunkOriginXZ = clamp(chunkOriginXZ, hfMin - chunkSize.xx, hfMax);
	return true;
}

// -----------------------------------------------------------------------------
// Height / Density / Interaction sampling
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

// -----------------------------------------------------------------------------
// Frustum culling (AABB vs 6 planes)
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
		{
			return false;
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Distance scaling (stable thinning gate, NOT truncation)
// - Key change: we do NOT shrink "samplesThisChunk" based on camera.
//   We always iterate baseSamples, but use a deterministic keep probability.
// - This prevents camera movement from changing which sample indices exist,
//   so world placement is stable.
// -----------------------------------------------------------------------------
float ComputeDistanceKeep01(float distSqr, float lod0Sqr, float spawnRadiusSqr)
{
	float dist = sqrt(max(distSqr, 0.0f));
	float lod0 = sqrt(max(lod0Sqr, 0.0f));
	float spawnR = sqrt(max(spawnRadiusSqr, 0.0f));

	float t = (dist - lod0) / max(spawnR - lod0, 1e-6f);
	t = saturate(t);

	return 1.0f - t;
}

// -----------------------------------------------------------------------------
// Mesh counter reserve helpers (wave optimized, grouped by meshId)
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
	{
		return 0u + firstbitlow(b.M.x);
	}
	if (b.M.y != 0u)
	{
		return 32u + firstbitlow(b.M.y);
	}
	if (b.M.z != 0u)
	{
		return 64u + firstbitlow(b.M.z);
	}
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
		{
			myBase = base;
		}

		remaining = BallotAndNot(remaining, group);
	}

	return myBase;
}

// -----------------------------------------------------------------------------
// Per-instance random helpers (deterministic)
// -----------------------------------------------------------------------------
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
// Shared spawn logic (must match between Count and Build)
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
	float Keep01;
};

bool BuildChunkContext(uint cellIndex, out ChunkContext ctx)
{
	uint dim = g_CB.ChunkVisibleDim;
	uint visibleCells = dim * dim;

	if (cellIndex >= visibleCells)
	{
		return false;
	}

	VisibleCell cell = g_VisibleCellTable[cellIndex];
	int2 chunkCoord = cell.ChunkCoord;

	float2 chunkOriginXZ = ChunkCoordToWorldOrigin(chunkCoord, g_CB.ChunkSize);
	float2 chunkOriginClamped = chunkOriginXZ;

	if (!ClampChunkToHeightfield(chunkOriginClamped, g_CB.ChunkSize))
	{
		return false;
	}

	float chunkHeight = g_PoolChunkCoord[cellIndex].ChunkHeight;
	if (chunkHeight == 0.0f)
	{
		chunkHeight = SampleWorldHeightAtWorldXZLevel(chunkOriginClamped, 5.0f);
	}

	float3 chunkMin = float3(chunkOriginClamped.x, chunkHeight - CHUNK_AABB_HALF_Y, chunkOriginClamped.y);
	float3 chunkMax = float3(chunkOriginClamped.x + g_CB.ChunkSize, chunkHeight + CHUNK_AABB_HALF_Y, chunkOriginClamped.y + g_CB.ChunkSize);

	float3 ex = float3(0.5, 0.5, 0.5);
	if (!AabbInsideFrustum(chunkMin - ex, chunkMax + ex))
	{
		return false;
	}

	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 chunkCenterXZ = chunkOriginClamped + 0.5f * g_CB.ChunkSize.xx;

	float2 dChunk = chunkCenterXZ - camXZ;
	float distChunkSqr = dot(dChunk, dChunk);

	float spawnRadiusSqr = g_CB.SpawnRadius * g_CB.SpawnRadius;
	if (distChunkSqr > spawnRadiusSqr)
	{
		return false;
	}

	float lod0Sqr = g_CB.LOD0Distance * g_CB.LOD0Distance;
	float lod1Sqr = g_CB.LOD1Distance * g_CB.LOD1Distance;

	float chunkDensity = SampleWorldDensity(chunkCenterXZ, CHUNK_DENSITY_MIP);
	if (chunkDensity <= DENSITY_DISABLE_THRESHOLD)
	{
		return false;
	}

	float keep01 = ComputeDistanceKeep01(distChunkSqr, lod0Sqr, spawnRadiusSqr);
	keep01 *= saturate(chunkDensity);

	ctx.ChunkCoord = chunkCoord;
	ctx.ChunkOriginClampedXZ = chunkOriginClamped;
	ctx.ChunkCenterXZ = chunkCenterXZ;
	ctx.DistChunkSqr = distChunkSqr;
	ctx.SpawnRadiusSqr = spawnRadiusSqr;
	ctx.Lod0Sqr = lod0Sqr;
	ctx.Lod1Sqr = lod1Sqr;
	ctx.ChunkDensity = chunkDensity;
	ctx.Keep01 = keep01;

	return true;
}

bool GrassInstanceAabbInsideFrustum(float3 posWS, float scale)
{
	// Make bounds conservative. Tune these multipliers to your content.
	float halfXZ = 0.5 * scale; // was 0.5
	float minY = -0.05 * scale; // allow bending below origin
	float maxY = 1.05 * scale;

	float3 bmin = posWS + float3(-halfXZ, minY, -halfXZ);
	float3 bmax = posWS + float3(halfXZ, maxY, halfXZ);

	// Extra pad helps screen-edge precision issues.
	float pad = 0.5f * scale;
	bmin -= pad.xxx;
	bmax += pad.xxx;

	return AabbInsideFrustum(bmin, bmax);
}

float2 ComputeYawPitchFromNormal(float3 nWS, float yaw)
{
	nWS = normalize(nWS);

	float s = sin(yaw);
	float c = cos(yaw);

	// Inverse yaw rotation around Y (bring normal into "local yaw" frame)
	float3 nL;
	nL.x = c * nWS.x + s * nWS.z;
	nL.y = nWS.y;
	nL.z = -s * nWS.x + c * nWS.z;

	float pitch = atan2(nL.z, max(nL.y, 1e-6f));

	return float2(yaw, pitch);
}

// Fix: silence -Wparameter-usage by initializing all out params on every return path.
// Do this at the top of EvaluateSpawn(), or before each early-return.
//
// Replace your EvaluateSpawn() with this version.

bool EvaluateSpawn(
	ChunkContext ctx,
	uint poolBase,
	uint sampleIndex,
	out uint outSpeciesId,
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
	// -------------------------------------------------------------------------
	// Initialize out parameters (required by some compilers' static analysis).
	// -------------------------------------------------------------------------
	outSpeciesId = 0u;
	outLodIndex = 0u;
	outPress01 = 0.0f;
	outScale = 0.0f;
	outYaw = 0.0f;
	outPitch = 0.0f;
	outBend01 = 0.0f;
	outSeed8 = 0u;
	outVariantId = 0u;
	outAtlasIndex = 0u;
	outPosWS = float3(0.0f, 0.0f, 0.0f);

	PoolEntry p = g_PoolPositions[poolBase + sampleIndex];

	if (isnan(p.Position.y))
	{
		// Keep outPosWS in a safe state.
		outPosWS = float3(0.0f, INVALID_NAN, 0.0f);
		return false;
	}

	uint speciesId = p.SpeciesId;
	if (speciesId >= g_CB.NumSpecies)
	{
		return false;
	}

	// Deterministic seed based only on world chunk + sample index
	uint chunkSeed = Hash2i(ctx.ChunkCoord, g_CB.SeedSalt);
	uint seed = WangHash(chunkSeed ^ (sampleIndex * 0x9E3779B9u));

	// Deterministic thinning (stable instance set)
	float keepGate = saturate(ctx.Keep01);
	if (Rand01(seed ^ 0x1010u) > keepGate)
	{
		return false;
	}

	float2 posXZ = p.Position.xz;

	float instDensity = SampleWorldDensity(posXZ, INSTANCE_DENSITY_MIP);
	if (instDensity <= DENSITY_DISABLE_THRESHOLD)
	{
		return false;
	}

	float spawnGateBase = saturate(g_CB.SpawnProb * instDensity);
	if (Rand01(seed ^ 0x4444u) > spawnGateBase)
	{
		return false;
	}

	float hN = SampleTerrainSurfaceHeight01AtWorldXZ(posXZ);
	float heightMask = ComputeHeightMask(hN);
	if (heightMask <= 0.001f)
	{
		return false;
	}

	float spawnGate = spawnGateBase * heightMask;
	if (spawnGate <= 0.001f)
	{
		return false;
	}

	float press01 = saturate(SampleInteraction(posXZ));

	if (Rand01(seed ^ 0x4A4Au) > spawnGate)
	{
		return false;
	}

	float3 posWS = p.Position;

	// LOD decision (representation can change with camera; instance set is stable)
	float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 dxz = posWS.xz - camXZ;
	float distSqr = dot(dxz, dxz);

	uint lodIndex =
		(distSqr < ctx.Lod0Sqr) ? 0u :
		(distSqr < ctx.Lod1Sqr) ? 1u : 2u;

	float scaleT = Rand01(seed ^ 0x5555u);
	float scale = lerp(g_CB.MinScale, g_CB.MaxScale, scaleT);

	float yaw = Rand01(seed ^ 0x6666u) * GRASS_TWO_PI;

	float3 terrainN = SampleTerrainNormalAtWorldXZLevel(posXZ, 0.0f);
	terrainN = normalize(terrainN);

	float slope01 = saturate(1.0f - terrainN.y);
	float align = saturate(slope01);
	align *= saturate(g_CB.NormalAlignStrength);

	float2 yp = ComputeYawPitchFromNormal(terrainN, yaw);
	float pitchFromNormal = yp.y;
	float pitch = pitchFromNormal * align;

	float bend01 = lerp(g_CB.BendStrengthMin, g_CB.BendStrengthMax, Rand01(seed ^ 0x8888u));

	uint seed8 = MakeSeed8(seed ^ 0x1234u);
	uint variantId = MakeVariantId(seed);
	uint atlasIndex = MakeAtlasIndex(seed);

	if (!GrassInstanceAabbInsideFrustum(posWS, scale))
	{
		return false;
	}

	// -------------------------------------------------------------------------
	// Commit out parameters only on success.
	// -------------------------------------------------------------------------
	outSpeciesId = speciesId;
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
// Entry A) UpdateChunkPoolsCS (poolIndex == cellIndex)
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Entry B) FillNewPoolsCS
// - Generates deterministic candidate points for each chunk
// - Species chosen uniformly (deterministic)
// -----------------------------------------------------------------------------
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
		uint base0 = pool * g_CB.SamplesPerChunk;

		for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
		{
			g_PoolPositions[base0 + s].Position = float3(0.0f, INVALID_NAN, 0.0f);
			g_PoolPositions[base0 + s].SpeciesId = 0u;
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

		float y = SampleTerrainSurfaceHeightAtWorldXZ(posXZ) + g_CB.YOffset;

		uint speciesId = WangHash(seed ^ 0xABCDEFu) % numSpecies;

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

// -----------------------------------------------------------------------------
// Entry B-1) ClearSpeciesCountersCS
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Entry B-2) CountInstancesFromPoolsCS
// - Stable spawn: iterate ALL base samples, deterministic keep gate.
// - This removes "camera-dependent instance set" popping.
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void CountInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
	uint cellIndex = gid.x;

	ChunkContext ctx;
	if (!BuildChunkContext(cellIndex, ctx))
	{
		return;
	}

	uint pool = cellIndex;
	uint base = pool * g_CB.SamplesPerChunk;

	for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
	{
		uint speciesId;
		uint lodIndex;
		float press01;
		float scale;
		float yaw;
		float pitch;
		float bend01;
		uint seed8;
		uint variantId;
		uint atlasIndex;
		float3 posWS;

		if (!EvaluateSpawn(ctx, base, s, speciesId, lodIndex, press01, scale, yaw, pitch, bend01, seed8, variantId, atlasIndex, posWS))
		{
			continue;
		}

		uint idx = speciesId * 3u + lodIndex;
		InterlockedAdd(g_SpeciesLodCounts[idx], 1u);
	}
}

// -----------------------------------------------------------------------------
// Entry B-3) PrefixSpeciesOffsetsCS
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Entry C) BuildInstancesFromPoolsCS
// - Uses the exact same EvaluateSpawn() as Count.
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void BuildInstancesFromPoolsCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
	uint cellIndex = gid.x;

	ChunkContext ctx;
	if (!BuildChunkContext(cellIndex, ctx))
	{
		return;
	}

	uint pool = cellIndex;
	uint base = pool * g_CB.SamplesPerChunk;

	for (uint s = tid.x; s < g_CB.SamplesPerChunk; s += 256u)
	{
		uint speciesId;
		uint lodIndex;
		float press01;
		float scale;
		float yaw;
		float pitch;
		float bend01;
		uint seed8;
		uint variantId;
		uint atlasIndex;
		float3 posWS;

		if (!EvaluateSpawn(ctx, base, s, speciesId, lodIndex, press01, scale, yaw, pitch, bend01, seed8, variantId, atlasIndex, posWS))
		{
			continue;
		}

		uint meshId =
			(lodIndex == 0u) ? g_SpeciesLOD0MeshId[speciesId] :
			(lodIndex == 1u) ? g_SpeciesLOD1MeshId[speciesId] :
							   g_SpeciesLOD2MeshId[speciesId];

		// Keep mesh counters for indirect draws (grouped wave atomic).
		WaveReserveMeshCounter_Grouped(meshId);

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
