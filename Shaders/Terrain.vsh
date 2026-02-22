#include "Common.hlsli"
#include "TerrainCommon.hlsli"

StructuredBuffer<TerrainDrawConstants> g_TerrainDrawConstants;

BASE_VS_MAIN_ENTRY(InstanceID)
{
	TerrainDrawConstants dc = g_TerrainDrawConstants[g_DrawCB.StartInstanceLocation + InstanceID];

	float3 vertexPosition = GET_VERTEX_POS();

	float2 grid01 = vertexPosition.xz * g_TerrainCB.InvChunkGridRes;

	// Use per-instance ChunkSizeXZ to avoid edge UV drift on partial border chunks.
	float2 worldXZ = dc.ChunkOriginXZ + grid01 * dc.ChunkSizeXZ;

	float step = (float) (1u << dc.LodIndex);
	float lod = max(log2(step), 0.0f);
	
	float y = SampleWorldHeightAtWorldXZ(worldXZ);
	float3 worldPos = float3(worldXZ.x, y, worldXZ.y);

	float2 uvWorld01 = WorldXZToTerrainUV(worldXZ);
	float3 normalWorld = SampleTerrainNormalAtWorldXZLevel(worldXZ, lod);

	float3 tangentWorld = normalize(float3(1, 0, 0) - normalWorld * normalWorld.x);

	SET_VSOUT_WORLD_POS_STATIC(float4(worldPos, 1.0f));
	SET_VSOUT_UV(uvWorld01);
	SET_VSOUT_WORLD_NORMAL(normalWorld);
	SET_VSOUT_WORLD_TANGENT(tangentWorld);
}