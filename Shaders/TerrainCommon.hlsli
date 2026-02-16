// ============================================================================
// TerrainCommon.hlsli
// ============================================================================
#include "Common.hlsli"

#ifndef TERRAIN_COMMON_HLSLI
#define TERRAIN_COMMON_HLSLI

// -----------------------------------------------------------------------------
// Resources
// -----------------------------------------------------------------------------
Texture2D<float> g_TerrainHeightTex;

Texture2D<float4> g_TerrainDiffuseTex;
Texture2D<float4> g_TerrainNormalTex;

Texture2D<float> g_TerrainSlopeTex;
Texture2D<float> g_TerrainFlowTex;
Texture2D<float> g_TerrainRockyTex;
Texture2D<float> g_TerrainSoilTex;
Texture2D<float> g_TerrainVegetationTex;

// -----------------------------------------------------------------------------
// Terrain common helpers
// -----------------------------------------------------------------------------
float2 WorldXZToTerrainUV(float2 worldXZ)
{
	float2 gridXZ = (worldXZ - g_TerrainCB.WorldOriginXZ) * g_TerrainCB.InvWorldSpacing;
	float2 uv = saturate((gridXZ + 0.5.xx) * g_TerrainCB.HeightTexelSize);

	float2 halfTexel = 0.5f * g_TerrainCB.HeightTexelSize;
	uv = clamp(uv, halfTexel, 1.0f - halfTexel);
	return uv;
}

// -----------------------------------------------------------------------------
// Height sampling (01)
// -----------------------------------------------------------------------------
float SampleTerrainHeight01(float2 uv)
{
	return g_TerrainHeightTex.SampleLevel(g_LinearClampSampler, uv, 0).r;
}

float SampleTerrainHeight01Level(float2 uv, float lod)
{
	return g_TerrainHeightTex.SampleLevel(g_LinearClampSampler, uv, lod).r;
}

float SampleTerrainHeight01AtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainHeight01(uv);
}

float SampleTerrainHeight01AtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainHeight01Level(uv, lod);
}

// -----------------------------------------------------------------------------
// Height sampling (world meters)
// -----------------------------------------------------------------------------
float SampleTerrainHeight(float2 uv)
{
	float h01 = SampleTerrainHeight01(uv);
	return g_TerrainCB.HeightOffset + h01 * g_TerrainCB.HeightScale;
}

float SampleTerrainHeightLevel(float2 uv, float lod)
{
	float h01 = SampleTerrainHeight01Level(uv, lod);
	return g_TerrainCB.HeightOffset + h01 * g_TerrainCB.HeightScale;
}

float SampleWorldHeightAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainHeight(uv);
}

float SampleWorldHeightAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainHeightLevel(uv, lod);
}

// -----------------------------------------------------------------------------
// Surface height (triangle-accurate interpolation) - KEEP
// -----------------------------------------------------------------------------
float SampleTerrainSurfaceHeight01AtWorldXZ(float2 worldXZ)
{
	float2 hfMin = g_TerrainCB.WorldOriginXZ;
	float2 hfMax = g_TerrainCB.WorldOriginXZ + g_TerrainCB.WorldSizeXZ;

	float2 eps = 1e-3.xx;
	worldXZ = clamp(worldXZ, hfMin + eps, hfMax - eps);

	float cellSize = g_TerrainCB.ChunkSize * g_TerrainCB.InvChunkGridRes;
	float2 cellSizeXZ = float2(cellSize, cellSize);

	float2 g = (worldXZ - g_TerrainCB.WorldOriginXZ) / max(cellSizeXZ, 1e-6.xx);

	int2 cell = int2((int) floor(g.x), (int) floor(g.y));
	float2 f = frac(g);

	float2 p00 = g_TerrainCB.WorldOriginXZ + float2(cell) * cellSizeXZ;
	float2 p10 = p00 + float2(cellSizeXZ.x, 0.0f);
	float2 p01 = p00 + float2(0.0f, cellSizeXZ.y);
	float2 p11 = p00 + cellSizeXZ;

	float h00 = SampleTerrainHeight01AtWorldXZLevel(p00, 0);
	float h10 = SampleTerrainHeight01AtWorldXZLevel(p10, 0);
	float h01 = SampleTerrainHeight01AtWorldXZLevel(p01, 0);
	float h11 = SampleTerrainHeight01AtWorldXZLevel(p11, 0);

	float fx = f.x;
	float fy = f.y;

	if (fx + fy <= 1.0f)
	{
		return h00 * (1.0f - fx - fy) + h10 * fx + h01 * fy;
	}
	else
	{
		float w11 = fx + fy - 1.0f;
		return h11 * w11 + h01 * (1.0f - fx) + h10 * (1.0f - fy);
	}
}

float SampleTerrainSurfaceHeightAtWorldXZ(float2 worldXZ)
{
	float2 hfMin = g_TerrainCB.WorldOriginXZ;
	float2 hfMax = g_TerrainCB.WorldOriginXZ + g_TerrainCB.WorldSizeXZ;

	float2 eps = 1e-3.xx;
	worldXZ = clamp(worldXZ, hfMin + eps, hfMax - eps);

	float cellSize = g_TerrainCB.ChunkSize * g_TerrainCB.InvChunkGridRes;
	float2 cellSizeXZ = float2(cellSize, cellSize);

	float2 g = (worldXZ - g_TerrainCB.WorldOriginXZ) / max(cellSizeXZ, 1e-6.xx);

	int2 cell = int2((int) floor(g.x), (int) floor(g.y));
	float2 f = frac(g);

	float2 p00 = g_TerrainCB.WorldOriginXZ + float2(cell) * cellSizeXZ;
	float2 p10 = p00 + float2(cellSizeXZ.x, 0.0f);
	float2 p01 = p00 + float2(0.0f, cellSizeXZ.y);
	float2 p11 = p00 + cellSizeXZ;

	float h00 = SampleWorldHeightAtWorldXZLevel(p00, 0);
	float h10 = SampleWorldHeightAtWorldXZLevel(p10, 0);
	float h01 = SampleWorldHeightAtWorldXZLevel(p01, 0);
	float h11 = SampleWorldHeightAtWorldXZLevel(p11, 0);

	float fx = f.x;
	float fy = f.y;

	if (fx + fy <= 1.0f)
	{
		return h00 * (1.0f - fx - fy) + h10 * fx + h01 * fy;
	}
	else
	{
		float w11 = fx + fy - 1.0f;
		return h11 * w11 + h01 * (1.0f - fx) + h10 * (1.0f - fy);
	}
}

// -----------------------------------------------------------------------------
// Diffuse
// -----------------------------------------------------------------------------
float4 SampleTerrainDiffuse(float2 uv)
{
	return g_TerrainDiffuseTex.SampleLevel(g_LinearClampSampler, uv, 0);
}

float4 SampleTerrainDiffuseLevel(float2 uv, float lod)
{
	return g_TerrainDiffuseTex.SampleLevel(g_LinearClampSampler, uv, lod);
}

float4 SampleTerrainDiffuseAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainDiffuse(uv);
}

float4 SampleTerrainDiffuseAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainDiffuseLevel(uv, lod);
}

// -----------------------------------------------------------------------------
// Normal (decoded to [-1..1])
// -----------------------------------------------------------------------------
float3 SampleTerrainNormal(float2 uv)
{
	return g_TerrainNormalTex.SampleLevel(g_LinearClampSampler, uv, 0).rgb * 2.0f - 1.0f;
}

float3 SampleTerrainNormalLevel(float2 uv, float lod)
{
	return g_TerrainNormalTex.SampleLevel(g_LinearClampSampler, uv, lod).rgb * 2.0f - 1.0f;
}

float3 SampleTerrainNormalAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainNormal(uv);
}

float3 SampleTerrainNormalAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainNormalLevel(uv, lod);
}

// -----------------------------------------------------------------------------
// Slope
// -----------------------------------------------------------------------------
float SampleTerrainSlope(float2 uv)
{
	return g_TerrainSlopeTex.SampleLevel(g_LinearClampSampler, uv, 0).r;
}

float SampleTerrainSlopeLevel(float2 uv, float lod)
{
	return g_TerrainSlopeTex.SampleLevel(g_LinearClampSampler, uv, lod).r;
}

float SampleTerrainSlopeAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainSlope(uv);
}

float SampleTerrainSlopeAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainSlopeLevel(uv, lod);
}

// -----------------------------------------------------------------------------
// Flow
// -----------------------------------------------------------------------------
float SampleTerrainFlow(float2 uv)
{
	return g_TerrainFlowTex.SampleLevel(g_LinearClampSampler, uv, 0).r;
}

float SampleTerrainFlowLevel(float2 uv, float lod)
{
	return g_TerrainFlowTex.SampleLevel(g_LinearClampSampler, uv, lod).r;
}

float SampleTerrainFlowAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainFlow(uv);
}

float SampleTerrainFlowAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainFlowLevel(uv, lod);
}

// -----------------------------------------------------------------------------
// Rocky
// -----------------------------------------------------------------------------
float SampleTerrainRocky(float2 uv)
{
	return g_TerrainRockyTex.SampleLevel(g_LinearClampSampler, uv, 0).r;
}

float SampleTerrainRockyLevel(float2 uv, float lod)
{
	return g_TerrainRockyTex.SampleLevel(g_LinearClampSampler, uv, lod).r;
}

float SampleTerrainRockyAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainRocky(uv);
}

float SampleTerrainRockyAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainRockyLevel(uv, lod);
}

// -----------------------------------------------------------------------------
// Soil
// -----------------------------------------------------------------------------
float SampleTerrainSoil(float2 uv)
{
	return g_TerrainSoilTex.SampleLevel(g_LinearClampSampler, uv, 0).r;
}

float SampleTerrainSoilLevel(float2 uv, float lod)
{
	return g_TerrainSoilTex.SampleLevel(g_LinearClampSampler, uv, lod).r;
}

float SampleTerrainSoilAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainSoil(uv);
}

float SampleTerrainSoilAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainSoilLevel(uv, lod);
}

// -----------------------------------------------------------------------------
// Vegetation
// -----------------------------------------------------------------------------
float SampleTerrainVegetation(float2 uv)
{
	return g_TerrainVegetationTex.SampleLevel(g_LinearClampSampler, uv, 0).r;
}

float SampleTerrainVegetationLevel(float2 uv, float lod)
{
	return g_TerrainVegetationTex.SampleLevel(g_LinearClampSampler, uv, lod).r;
}

float SampleTerrainVegetationAtWorldXZ(float2 worldXZ)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainVegetation(uv);
}

float SampleTerrainVegetationAtWorldXZLevel(float2 worldXZ, float lod)
{
	float2 uv = WorldXZToTerrainUV(worldXZ);
	return SampleTerrainVegetationLevel(uv, lod);
}

#endif // TERRAIN_COMMON_HLSLI