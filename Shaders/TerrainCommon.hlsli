#include "Common.hlsli"

#ifndef TERRAIN_COMMOM_HLSLI
#define TERRAIN_COMMOM_HLSLI

Texture2D g_TerrainHeightTex;
Texture2D g_TerrainDiffuseTex;
Texture2D g_TerrainNormalTex;
Texture2D g_TerrainSlopeTex;
Texture2D g_TerrainFlowTex;
Texture2D g_TerrainRockyTex;
Texture2D g_TerrainSoilTex;
Texture2D g_TerrainVegetationTex;

// Terrain mesh resolution per chunk is 16x16 quads.
static const float TERRAIN_CHUNK_GRID_RES = 16.0f;

// ----------------------------------------------
// Terrain common helpers
// ----------------------------------------------

float2 WorldXZToTerrainUV(float2 worldXZ)
{
	float2 gridXZ = (worldXZ - g_TerrainCB.WorldOriginXZ) / g_TerrainCB.WorldSpacingXZ;
	float2 uv = saturate((gridXZ + 0.5.xx) * g_TerrainCB.HeightTexelSize);
	float2 halfTexel = 0.5f * g_TerrainCB.HeightTexelSize;
	uv = clamp(uv, halfTexel, 1.0f - halfTexel);
	return uv;
}

// Sample height in [0..1] range
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

// Sample height in world units (meters)
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

// Sample height in [0..1] range, with triangle-accurate interpolation (matches the rendered mesh surface).
float SampleTerrainSurfaceHeight01AtWorldXZ(float2 worldXZ)
{
	// Clamp worldXZ into terrain bounds (avoid sampling outside).
	float2 hfMin = g_TerrainCB.WorldOriginXZ;
	float2 hfMax = g_TerrainCB.WorldOriginXZ + g_TerrainCB.WorldSizeXZ;

	// Keep a tiny margin to avoid hitting exact boundary issues.
	float2 eps = 1e-3.xx;
	worldXZ = clamp(worldXZ, hfMin + eps, hfMax - eps);

	// Cell size of ONE quad in the rendered mesh.
	float2 cellSize = g_TerrainCB.ChunkSizeXZ / TERRAIN_CHUNK_GRID_RES;

	// Convert to "mesh grid space" (in units of quads).
	float2 g = (worldXZ - g_TerrainCB.WorldOriginXZ) / max(cellSize, 1e-6.xx);

	int2 cell = int2((int) floor(g.x), (int) floor(g.y));
	float2 f = frac(g); // local position inside the quad [0..1)

	// Corner positions (world XZ) for the 4 grid vertices of this quad.
	float2 p00 = g_TerrainCB.WorldOriginXZ + float2(cell) * cellSize;
	float2 p10 = p00 + float2(cellSize.x, 0.0f);
	float2 p01 = p00 + float2(0.0f, cellSize.y);
	float2 p11 = p00 + cellSize;

	// Vertex heights must match VS sampling path.
	// In your VS: SampleWorldHeightAtWorldXZ(worldXZ) -> SampleLevel(0) in VS-safe TerrainCommon.
	float h00 = SampleTerrainHeight01AtWorldXZLevel(p00, 0);
	float h10 = SampleTerrainHeight01AtWorldXZLevel(p10, 0);
	float h01 = SampleTerrainHeight01AtWorldXZLevel(p01, 0);
	float h11 = SampleTerrainHeight01AtWorldXZLevel(p11, 0);

	// Triangle-accurate interpolation (matches how the mesh rasterizes the quad).
	float fx = f.x;
	float fy = f.y;

	// Diagonal: 00 -> 11
	if (fx + fy <= 1.0f)
	{
		// tri: (00,10,01)
		// bary: w00=1-fx-fy, w10=fx, w01=fy
		return h00 * (1.0f - fx - fy) + h10 * fx + h01 * fy;
	}
	else
	{
		// tri: (11,01,10)
		// bary: w11=fx+fy-1, w01=1-fx, w10=1-fy
		float w11 = fx + fy - 1.0f;
		return h11 * w11 + h01 * (1.0f - fx) + h10 * (1.0f - fy);
	}
}

float SampleTerrainSurfaceHeightAtWorldXZ(float2 worldXZ)
{
	// Clamp worldXZ into terrain bounds (avoid sampling outside).
	float2 hfMin = g_TerrainCB.WorldOriginXZ;
	float2 hfMax = g_TerrainCB.WorldOriginXZ + g_TerrainCB.WorldSizeXZ;

	// Keep a tiny margin to avoid hitting exact boundary issues.
	float2 eps = 1e-3.xx;
	worldXZ = clamp(worldXZ, hfMin + eps, hfMax - eps);

	// Cell size of ONE quad in the rendered mesh.
	float2 cellSize = g_TerrainCB.ChunkSizeXZ / TERRAIN_CHUNK_GRID_RES;

	// Convert to "mesh grid space" (in units of quads).
	float2 g = (worldXZ - g_TerrainCB.WorldOriginXZ) / max(cellSize, 1e-6.xx);

	int2 cell = int2((int) floor(g.x), (int) floor(g.y));
	float2 f = frac(g); // local position inside the quad [0..1)

	// Corner positions (world XZ) for the 4 grid vertices of this quad.
	float2 p00 = g_TerrainCB.WorldOriginXZ + float2(cell) * cellSize;
	float2 p10 = p00 + float2(cellSize.x, 0.0f);
	float2 p01 = p00 + float2(0.0f, cellSize.y);
	float2 p11 = p00 + cellSize;

	// Vertex heights must match VS sampling path.
	// In your VS: SampleWorldHeightAtWorldXZ(worldXZ) -> SampleLevel(0) in VS-safe TerrainCommon.
	float h00 = SampleWorldHeightAtWorldXZLevel(p00, 0);
	float h10 = SampleWorldHeightAtWorldXZLevel(p10, 0);
	float h01 = SampleWorldHeightAtWorldXZLevel(p01, 0);
	float h11 = SampleWorldHeightAtWorldXZLevel(p11, 0);

	// Triangle-accurate interpolation (matches how the mesh rasterizes the quad).
	float fx = f.x;
	float fy = f.y;

	// Diagonal: 00 -> 11
	if (fx + fy <= 1.0f)
	{
		// tri: (00,10,01)
		// bary: w00=1-fx-fy, w10=fx, w01=fy
		return h00 * (1.0f - fx - fy) + h10 * fx + h01 * fy;
	}
	else
	{
		// tri: (11,01,10)
		// bary: w11=fx+fy-1, w01=1-fx, w10=1-fy
		float w11 = fx + fy - 1.0f;
		return h11 * w11 + h01 * (1.0f - fx) + h10 * (1.0f - fy);
	}
}

// Sample diffuse color
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

// Sample normal in [-1..1] range
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

// Sample slope
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

// Sample flow
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

// Sample rocky mask
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

// Sample soil mask
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

// Sample vegetation mask
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

#endif // TERRAIN_COMMOM_HLSLI
