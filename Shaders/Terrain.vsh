#include "Common.hlsli"
#include "TerrainCommon.hlsli"

StructuredBuffer<TerrainDrawConstants> g_TerrainDrawConstants;

// Snap a world position to a coarser grid (used for LOD morph).
static float2 SnapWorldXZ(float2 worldXZ, TerrainConstants hf, float stepMul)
{
	float2 cell = hf.WorldSpacingXZ * stepMul;
	float2 g = (worldXZ - hf.WorldOriginXZ) / max(cell, 1e-6.xx);
	float2 gi = floor(g + 0.5.xx);
	return hf.WorldOriginXZ + gi * cell;
}

// Compute a terrain normal via central differences.
// Uses height sampling at a mip roughly matching stepMul to reduce aliasing.
static float3 ComputeNormalAt(float2 worldXZ, float stepMul, TerrainDrawConstants terrainDrawCB)
{
	float2 spacing = g_TerrainCB.WorldSpacingXZ * stepMul;

	float2 dx = float2(spacing.x, 0.0f);
	float2 dz = float2(0.0f, spacing.y);

	// Map stepMul (1,2,4,8...) to mip level (0,1,2,3...).
	float lod = max(log2(max(stepMul, 1.0f)), 0.0f);

	float hL = SampleWorldHeightAtWorldXZLevel(worldXZ - dx, lod);
	float hR = SampleWorldHeightAtWorldXZLevel(worldXZ + dx, lod);
	float hD = SampleWorldHeightAtWorldXZLevel(worldXZ - dz, lod);
	float hU = SampleWorldHeightAtWorldXZLevel(worldXZ + dz, lod);

	float dHdx = (hR - hL) / max(2.0f * spacing.x, 1e-6f);
	float dHdz = (hU - hD) / max(2.0f * spacing.y, 1e-6f);

	float up = max(0.001f, g_TerrainCB.NormalUpBias);
	return normalize(float3(-dHdx, up, -dHdz));
}

BASE_VS_MAIN_ENTRY(InstanceID)
{
	TerrainDrawConstants terrainDrawCB = g_TerrainDrawConstants[g_DrawCB.StartInstanceLocation + InstanceID];

	float3 vertexPosition = GET_VERTEX_POS();

	// Local grid [0..1] within this chunk (assuming 16x16 grid like before).
	float2 grid01 = vertexPosition.xz * (1.0f / 16.0f);

	// Chunk-local -> world XZ (meters)
	float2 worldXZ = terrainDrawCB.ChunkOriginXZ + grid01 * terrainDrawCB.ChunkSizeXZ;

	// LOD morph: fine/coarse step in world-grid units.
	float stepFine = (float) (1u << terrainDrawCB.LodIndex); // 1, 2, 4, 8, ...
	float stepCoarse = stepFine * 2.0f;

	float alpha = saturate(terrainDrawCB.LodMorphAlpha);
	if (terrainDrawCB.LodIndex >= 4)
	{
		alpha = 0.0f;
	}

	// Height: blend between coarse snapped and fine.
	float hFine = SampleWorldHeightAtWorldXZ(worldXZ);

	float2 worldXZCoarse = SnapWorldXZ(worldXZ, g_TerrainCB, stepCoarse);
	float hCoarse = SampleWorldHeightAtWorldXZ(worldXZCoarse);

	float wy = lerp(hCoarse, hFine, alpha);

	float3 worldPos = float3(worldXZ.x, wy, worldXZ.y);

	// UV over full terrain [0..1] (optional tiling can be applied later in PS)
	float2 uvWorld01 = (worldXZ - g_TerrainCB.WorldOriginXZ) / max(g_TerrainCB.WorldSizeXZ, 1e-6.xx);

	// Normal (blend fine/coarse)
	float3 nFine = ComputeNormalAt(worldXZ, stepFine, terrainDrawCB);
	float3 nCoarse = ComputeNormalAt(worldXZCoarse, stepCoarse, terrainDrawCB);
	float3 nWS = normalize(lerp(nCoarse, nFine, alpha));

	// Simple tangent (world X axis projected to tangent plane)
	float3 tWS = float3(1.0f, 0.0f, 0.0f);
	tWS = normalize(tWS - nWS * dot(nWS, tWS));

	SET_VSOUT_WORLD_POS_STATIC(float4(worldPos, 1.0f));
	SET_VSOUT_UV(uvWorld01);
	SET_VSOUT_WORLD_NORMAL(nWS);
	SET_VSOUT_WORLD_TANGENT(tWS);
}