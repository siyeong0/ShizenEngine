// ============================================================================
// GrassGenerateInstances.hlsl
// Chunk-based grass instance generation (LOD0 Mesh / LOD1 CrossPlane / LOD2 Billboard)
// Wave-coalesced atomics into ByteAddress counters
// ============================================================================

#include "HLSL_Structures.hlsli"
#include "HeightField.hlsli"
#include "GrassCommon.hlsli"

// ---------------------------------------------------------------------------
// Constant buffers
// ---------------------------------------------------------------------------
cbuffer FRAME_CONSTANTS
{
	FrameConstants g_FrameCB;
};

cbuffer HEIGHT_FIELD_CONSTANTS
{
	HeightFieldConstants g_HeightFieldCB;
};

cbuffer GRASS_GEN_CONSTANTS
{
	GrassGenConstants g_GrassGenCB;
};

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------
RWStructuredBuffer<GrassMeshInstance> g_OutInstancesLOD0;
RWStructuredBuffer<GrassCrossPlaneInstance> g_OutInstancesLOD1;
RWStructuredBuffer<GrassBillboardInstance> g_OutInstancesLOD2;

// uint counter per slot (4 bytes) at byte offset (slot * 4)
RWByteAddressBuffer g_CounterBuffer;

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------
// HeightField (R16_UNORM sampled as normalized float 0..1)
Texture2D<float> g_HeightField;

// Density field (recommend: R8_UNORM)
Texture2D<float> g_DensityField;

// Interaction field (0..1). 1 = heavily pressed.
Texture2D<float> g_InteractionField;

// Samplers
SamplerState g_LinearClampSampler;
SamplerState g_LinearWrapSampler;

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
// Density sampling (world-tiled) + remap curve
// ---------------------------------------------------------------------------
float RemapDensity(float d, float contrast01)
{
	float a = saturate(contrast01);
	a = min(a, 0.49f);
	return smoothstep(a, 1.0f - a, d);
}

float SampleWorldDensity(float2 worldXZ)
{
	float tiling = (g_GrassGenCB.DensityTiling > 0.0f) ? g_GrassGenCB.DensityTiling : 0.002f;
	float contrast = (g_GrassGenCB.DensityContrast > 0.0f) ? g_GrassGenCB.DensityContrast : 0.25f;
	float powK = (g_GrassGenCB.DensityPow > 0.0f) ? g_GrassGenCB.DensityPow : 0.65f;

	float2 uv = worldXZ * tiling;
	float d = g_DensityField.SampleLevel(g_LinearWrapSampler, uv, 0.0f).r;

	d = saturate(d);
	d = RemapDensity(d, contrast);
	d = pow(d, powK);

	return d;
}

// ---------------------------------------------------------------------------
// Height/Interaction sampling (shared mapping rules with Terrain.vsh)
// ---------------------------------------------------------------------------
float2 WorldXZToHeightUV(float2 worldXZ)
{
    // Grass uses base mapping: scale=1, bias=0
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
	return HF_SampleWorldHeight(g_HeightField, g_LinearClampSampler, uv, g_HeightFieldCB, g_GrassGenCB.YOffset);
}

float SampleInteraction(float2 worldXZ)
{
    // Local window uv (0..1)
	float2 uvLocal = (worldXZ - g_GrassGenCB.InteractionOriginXZ) * g_GrassGenCB.InteractionInvWorldSizeXZ;

    // Convert TexelOrigin to normalized uv offset
	float2 uvRingOffset = float2(g_GrassGenCB.InteractionTexelOrigin) * g_GrassGenCB.InteractionInvFieldSize;

    // Ring-space uv
	float2 uv = frac(uvLocal + uvRingOffset);

    // IMPORTANT:
    // uv is wrap-space; use wrap sampler to avoid seams/jumps at 0/1 boundary.
	return g_InteractionField.SampleLevel(g_LinearWrapSampler, uv, 0.0f).r;
}

// ---------------------------------------------------------------------------
// Slope / Height masks
// ---------------------------------------------------------------------------
float ComputeSlope01(float2 worldXZ)
{
	float2 e = g_HeightFieldCB.WorldSpacingXZ;

	float hX1 = SampleHeightNormalized(worldXZ + float2(e.x, 0.0f));
	float hX0 = SampleHeightNormalized(worldXZ - float2(e.x, 0.0f));
	float hZ1 = SampleHeightNormalized(worldXZ + float2(0.0f, e.y));
	float hZ0 = SampleHeightNormalized(worldXZ - float2(0.0f, e.y));

	float dhdx = (hX1 - hX0) * g_HeightFieldCB.HeightScale / max(2.0f * e.x, 1e-6f);
	float dhdz = (hZ1 - hZ0) * g_HeightFieldCB.HeightScale / max(2.0f * e.y, 1e-6f);

	float s = length(float2(dhdx, dhdz));

	float slopeTo01 = (g_GrassGenCB.SlopeToDensity > 0.0f) ? g_GrassGenCB.SlopeToDensity : 0.15f;
	return saturate(s * slopeTo01);
}

float ComputeHeightMask(float hN)
{
	float hMinN = g_GrassGenCB.HeightMinN;
	float hMaxN = g_GrassGenCB.HeightMaxN;
	float hFadeN = max(g_GrassGenCB.HeightFadeN, 1e-6f);

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
// Chunk grid mapping (heightfield-space aligned)
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
// Wave helpers (ballot wrapper + per-LOD wave reserve/scatter)
// ---------------------------------------------------------------------------
struct BallotMask
{
	uint4 M; // supports up to 128 lanes
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

// Reserve a contiguous range in a ByteAddress counter for this wave and broadcast base.
// counterByteOffset must be 4-byte aligned.
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
// Instance packing helpers for this CS
// ---------------------------------------------------------------------------
uint MakeSeed8(uint seed)
{
	return (WangHash(seed) >> 24) & 0xFFu;
}

uint MakeVariantId(uint seed)
{
    // If you later have multiple grass assets/atlases, map it here.
    // For now: 0..3 variation.
	return (WangHash(seed ^ 0xBEEFu) >> 30) & 0x3u;
}

uint MakeAtlasIndex(uint seed)
{
    // Placeholder: 0..7 frames
	return (WangHash(seed ^ 0xCAFEu) >> 29) & 0x7u;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void GenerateGrassInstances(uint3 tid : SV_DispatchThreadID)
{
	int halfExt = g_GrassGenCB.ChunkHalfExtent;
	int2 chunkGrid = int2((int) tid.x - halfExt, (int) tid.y - halfExt);

	// float2 camXZ = float2(g_FrameCB.CameraPosition.x, g_FrameCB.CameraPosition.z);
	float2 camXZ = float2(0,0);

	float chunkSize = g_GrassGenCB.ChunkSize;

	int2 camChunk = WorldXZToChunkCoord(camXZ, chunkSize);
	int2 worldChunk = camChunk + chunkGrid;

	float2 chunkOriginXZ = ChunkCoordToWorldOrigin(worldChunk, chunkSize);

	if (!ClampChunkToHeightfield(chunkOriginXZ, chunkSize))
	{
		return;
	}

	float chunkOriginHeight = SampleWorldHeight(chunkOriginXZ);

	float3 chunkMin = float3(chunkOriginXZ.x, chunkOriginHeight - 20.0f, chunkOriginXZ.y);
	float3 chunkMax = float3(chunkOriginXZ.x + chunkSize, chunkOriginHeight + 20.0f, chunkOriginXZ.y + chunkSize);

	if (!AabbInsideFrustum(chunkMin, chunkMax))
	{
		return;
	}

	uint chunkSeed = Hash2i(worldChunk, g_GrassGenCB.SeedSalt);

	float spawnRadiusSqr = g_GrassGenCB.SpawnRadius * g_GrassGenCB.SpawnRadius;
	float lod0Sqr = g_GrassGenCB.LOD0Distance * g_GrassGenCB.LOD0Distance;
	float lod1Sqr = g_GrassGenCB.LOD1Distance * g_GrassGenCB.LOD1Distance;

	uint counterOff0 = (g_GrassGenCB.IndirectSlotLOD0 << 2);
	uint counterOff1 = (g_GrassGenCB.IndirectSlotLOD1 << 2);
	uint counterOff2 = (g_GrassGenCB.IndirectSlotLOD2 << 2);

    [loop]
	for (uint s = 0u; s < g_GrassGenCB.SamplesPerChunk; ++s)
	{
        // -------------------------------------------------------------------
        // Candidate (stable) random per (chunk, sample)
        // -------------------------------------------------------------------
		uint seed = WangHash(chunkSeed ^ (s * 0x9E3779B9u));

		float ux = Rand01(seed ^ 0x2222u);
		float uz = Rand01(seed ^ 0x3333u);

		float jx = (ux - 0.5f) * g_GrassGenCB.Jitter;
		float jz = (uz - 0.5f) * g_GrassGenCB.Jitter;

		float2 localXZ = (float2(ux, uz) + float2(jx, jz)) * chunkSize;
		float2 posXZ = chunkOriginXZ + localXZ;

		float2 dc = posXZ - camXZ;
		float distanceCameraSqr = dot(dc, dc);

		bool emit = true;

		if (distanceCameraSqr > spawnRadiusSqr)
		{
			emit = false;
		}

		float density = 0.0f;
		float hN = 0.0f;
		float slope01 = 0.0f;
		float press01 = 0.0f;

		if (emit)
		{
			density = SampleWorldDensity(posXZ);
			if (density <= 0.001f)
			{
				emit = false;
			}
		}

		if (emit)
		{
			hN = SampleHeightNormalized(posXZ);
			slope01 = ComputeSlope01(posXZ);

			float slopeMask = 1.0f - slope01;
			float heightMask = ComputeHeightMask(hN);

			density *= saturate(slopeMask) * heightMask;
			if (density <= 0.001f)
			{
				emit = false;
			}
		}

		if (emit)
		{
			press01 = saturate(SampleInteraction(posXZ));

            // Additional thinning based on density (blue-noise-like)
			if (Rand01(seed ^ 0x41A7u) > density)
			{
				emit = false;
			}
		}

		if (emit)
		{
			float effectiveProb = saturate(g_GrassGenCB.SpawnProb * density);
			if (Rand01(seed ^ 0x4444u) > effectiveProb)
			{
				emit = false;
			}
		}

        // -------------------------------------------------------------------
        // Build per-instance params (only if emit)
        // -------------------------------------------------------------------
		float3 posWS = float3(0.0f, 0.0f, 0.0f);
		float scale = 0.0f;
		float yaw = 0.0f;
		float pitch = 0.0f;
		float bend01 = 0.0f;

		uint seed8 = 0u;
		uint variantId = 0u;
		uint atlasIndex = 0u;

		if (emit)
		{
			float y = SampleWorldHeight(posXZ);
			posWS = float3(posXZ.x, y, posXZ.y);

			float scaleT = Rand01(seed ^ 0x5555u);
			float densityScaleBias = lerp(1.2f, 0.8f, density);

			scale =
                lerp(g_GrassGenCB.MinScale, g_GrassGenCB.MaxScale, scaleT) *
                0.04f *
                densityScaleBias;

			yaw = Rand01(seed ^ 0x6666u) * GRASS_TWO_PI;
			pitch = lerp(g_GrassGenCB.MinPitch, g_GrassGenCB.MaxPitch, Rand01(seed ^ 0x7777u));

			bend01 = lerp(g_GrassGenCB.BendStrengthMin, g_GrassGenCB.BendStrengthMax, Rand01(seed ^ 0x8888u));

			seed8 = MakeSeed8(seed ^ 0x1234u);
			variantId = MakeVariantId(seed);
			atlasIndex = MakeAtlasIndex(seed);
		}

        // -------------------------------------------------------------------
        // LOD decide
        // -------------------------------------------------------------------
		bool emit0 = emit && (distanceCameraSqr < lod0Sqr);
		bool emit1 = emit && (!emit0) && (distanceCameraSqr < lod1Sqr);
		bool emit2 = emit && (!emit0) && (!emit1);

        // -------------------------------------------------------------------
        // Wave-coalesced reserve per LOD
        // -------------------------------------------------------------------
		BallotMask b0 = WaveBallotWrap(emit0);
		BallotMask b1 = WaveBallotWrap(emit1);
		BallotMask b2 = WaveBallotWrap(emit2);

		uint base0 = WaveReserveByteAddressCounter(counterOff0, b0);
		uint base1 = WaveReserveByteAddressCounter(counterOff1, b1);
		uint base2 = WaveReserveByteAddressCounter(counterOff2, b2);

		uint lane = WaveGetLaneIndex();

        // -------------------------------------------------------------------
        // Scatter
        // -------------------------------------------------------------------
		if (emit0)
		{
			uint p = BallotPrefixCountBits(b0, lane);

			GrassMeshInstance outInst = MakeGrassMeshInstance(
                posWS,
                scale,
                yaw,
                pitch,
                bend01,
                press01,
                variantId,
                seed8);

			g_OutInstancesLOD0[base0 + p] = outInst;
		}
		else if (emit1)
		{
			uint p = BallotPrefixCountBits(b1, lane);

			GrassCrossPlaneInstance outInst = MakeGrassCrossPlaneInstance(
                posWS,
                scale,
                yaw,
                bend01,
                press01,
                variantId,
                seed8,
                0u,
                0u);

			g_OutInstancesLOD1[base1 + p] = outInst;
		}
		else if (emit2)
		{
			uint p = BallotPrefixCountBits(b2, lane);

			GrassBillboardInstance outInst = MakeGrassBillboardInstance(
                posWS,
                scale,
                yaw,
                atlasIndex,
                seed8);

			g_OutInstancesLOD2[base2 + p] = outInst;
		}
	}
}
