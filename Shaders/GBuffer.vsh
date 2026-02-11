#include "Common.hlsli"

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
BASE_VS_MAIN_ENTRY(InstanceID)
{
	ObjectConstants oc = g_ObjectTable[g_DrawCB.StartInstanceLocation + InstanceID];

	float3 vertexPosition = GET_VERTEX_POS();
	float2 vertexUV = GET_VERTEX_UV();
	float3 vertexNormal = GET_VERTEX_NORMAL();
	float3 vertexTangent = GET_VERTEX_TANGENT();
	
	// World position
	float4 worldPos = mul(float4(vertexPosition, 1.0), oc.World);
	float4 prevWorldPos = mul(float4(vertexPosition, 1.0), oc.PrevWorld);

	// World-space normal/tangent
	float3 worldNormal = normalize(mul(vertexNormal, (float3x3) oc.WorldInvTranspose));
	float3 worldTangent = normalize(mul(vertexTangent, (float3x3) oc.WorldInvTranspose));

	// Orthonormalize tangent against normal
	worldTangent = normalize(worldTangent - worldNormal * dot(worldNormal, worldTangent));

	SET_VSOUT_WORLD_POS_DYNAMIC(worldPos, prevWorldPos);
	SET_VSOUT_UV(vertexUV);
	SET_VSOUT_WORLD_NORMAL(worldNormal);
	SET_VSOUT_WORLD_TANGENT(worldTangent);
}
