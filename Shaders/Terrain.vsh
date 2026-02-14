#include "Common.hlsli"
#include "TerrainCommon.hlsli"

StructuredBuffer<TerrainDrawConstants> g_TerrainDrawConstants;

Texture2D<float> g_HeightField;

static float2 snapWorldXZ(float2 worldXZ, TerrainConstants hf, float stepMul)
{
	float2 cell = hf.WorldSpacingXZ * stepMul;
	float2 g = (worldXZ - hf.WorldOriginXZ) / max(cell, 1e-6.xx);
	float2 gi = floor(g + 0.5.xx);
	return hf.WorldOriginXZ + gi * cell;
}

static float sampleWorldHeightAt(float2 worldXZ, TerrainDrawConstants terrainDrawCB)
{
	return HF_SampleWorldHeightAtWorldXZ(
		g_HeightField,
		g_LinearClampSampler,
		worldXZ,
		0.0,
		0.0);
}

static float3 computeNormalAt(float2 worldXZ, float stepMul, TerrainDrawConstants terrainDrawCB)
{
	float2 spacing = g_TerrainCB.WorldSpacingXZ * stepMul;

	float2 dx = float2(spacing.x, 0.0);
	float2 dz = float2(0.0, spacing.y);

	float hL = sampleWorldHeightAt(worldXZ - dx, terrainDrawCB);
	float hR = sampleWorldHeightAt(worldXZ + dx, terrainDrawCB);
	float hD = sampleWorldHeightAt(worldXZ - dz, terrainDrawCB);
	float hU = sampleWorldHeightAt(worldXZ + dz, terrainDrawCB);

	float dHdx = (hR - hL) / max(2.0 * spacing.x, 1e-6);
	float dHdz = (hU - hD) / max(2.0 * spacing.y, 1e-6);

	float up = max(0.001, g_TerrainCB.NormalUpBias);
	return normalize(float3(-dHdx, up, -dHdz));
}

BASE_VS_MAIN_ENTRY(InstanceID)
{
	TerrainDrawConstants terrainDrawCB = g_TerrainDrawConstants[g_DrawCB.StartInstanceLocation + InstanceID];
	
	float3 vertexPosition = GET_VERTEX_POS();
	
	// Local grid [0..1] within this chunk
	float2 grid01 = vertexPosition.xz * (1.0 / 16.0);

	// Chunk-local -> world XZ (meters)
	float2 worldXZ = terrainDrawCB.ChunkOriginXZ + grid01 * terrainDrawCB.ChunkSizeXZ;

	// LOD morph
	float stepFine = float(1 << terrainDrawCB.LodIndex); // 1, 2, 4, 8, ...
	float stepCoarse = stepFine * 2.0;

	float alpha = saturate(terrainDrawCB.LodMorphAlpha);
	if (terrainDrawCB.LodIndex >= 4)
		alpha = 0.0;

	float hFine = sampleWorldHeightAt(worldXZ, terrainDrawCB);

	float2 worldXZCoarse = snapWorldXZ(worldXZ, g_TerrainCB, stepCoarse);
	float hCoarse = sampleWorldHeightAt(worldXZCoarse, terrainDrawCB);

	float wy = lerp(hCoarse, hFine, alpha);

	float3 worldPos = float3(worldXZ.x, wy, worldXZ.y);

	// Surface UV: entire terrain mapped to [0..1] then optional tiling
	float2 uvWorld01 = (worldXZ - g_TerrainCB.WorldOriginXZ) / max(g_TerrainCB.WorldSizeXZ, 1e-6.xx);

	// Normal + tangent
	float3 NFine = computeNormalAt(worldXZ, stepFine, terrainDrawCB);
	float3 NCoarse = computeNormalAt(worldXZCoarse, stepCoarse, terrainDrawCB);
	float3 N = normalize(lerp(NCoarse, NFine, alpha));

	float3 T = float3(1.0, 0.0, 0.0);
	T = normalize(T - N * dot(N, T));

	SET_VSOUT_WORLD_POS_STATIC(float4(worldPos, 1.0f));
	SET_VSOUT_UV(uvWorld01);
	SET_VSOUT_WORLD_NORMAL(N);
	SET_VSOUT_WORLD_TANGENT(T);
}
