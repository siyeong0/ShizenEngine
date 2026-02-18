#include "Common.hlsli"
#include "TerrainCommon.hlsli"

StructuredBuffer<TerrainDrawConstants> g_TerrainDrawConstants;

static float3 ComputeNormalAt(float2 worldXZ, float stepMul)
{
	float spacing = g_TerrainCB.WorldSpacing * stepMul;

	float2 dx = float2(spacing, 0.0f);
	float2 dz = float2(0.0f, spacing);

	float lod = max(log2(max(stepMul, 1.0f)), 0.0f);

	float hL = SampleWorldHeightAtWorldXZLevel(worldXZ - dx, lod);
	float hR = SampleWorldHeightAtWorldXZLevel(worldXZ + dx, lod);
	float hD = SampleWorldHeightAtWorldXZLevel(worldXZ - dz, lod);
	float hU = SampleWorldHeightAtWorldXZLevel(worldXZ + dz, lod);

	float dHdx = (hR - hL) / max(2.0f * spacing, 1e-6f);
	float dHdz = (hU - hD) / max(2.0f * spacing, 1e-6f);

	float up = max(0.001f, g_TerrainCB.NormalUpBias);
	return normalize(float3(-dHdx, up, -dHdz));
}

BASE_VS_MAIN_ENTRY(InstanceID)
{
	TerrainDrawConstants dc = g_TerrainDrawConstants[g_DrawCB.StartInstanceLocation + InstanceID];

	float3 vertexPosition = GET_VERTEX_POS();

	float2 grid01 = vertexPosition.xz * g_TerrainCB.InvChunkGridRes;

    // Use per-instance ChunkSizeXZ to avoid edge UV drift on partial border chunks.
	float2 worldXZ = dc.ChunkOriginXZ + grid01 * dc.ChunkSizeXZ;

	float step = (float) (1u << dc.LodIndex);

	float y = SampleWorldHeightAtWorldXZ(worldXZ);
	float3 worldPos = float3(worldXZ.x, y, worldXZ.y);

	float2 uvWorld01 = WorldXZToTerrainUV(worldXZ);

	float3 normalWorld = SampleTerrainNormalAtWorldXZ(worldXZ);

	float3 tangentWorld = float3(1.0f, 0.0f, 0.0f);
	tangentWorld = normalize(tangentWorld - normalWorld * dot(normalWorld, tangentWorld));

    SET_VSOUT_WORLD_POS_STATIC(float4(worldPos, 1.0f));
    SET_VSOUT_UV(uvWorld01);
    SET_VSOUT_WORLD_NORMAL(normalWorld);
    SET_VSOUT_WORLD_TANGENT(tangentWorld);
}