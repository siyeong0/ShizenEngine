#include "Common.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<uint> g_SpeciesLodOffsets;

StructuredBuffer<GrassBillboardInstance> g_GrassInstances;

void GetCameraBasisWS(out float3 rightWS, out float3 upWS, out float3 forwardWS)
{
	rightWS = float3(g_FrameCB.InvView._11, g_FrameCB.InvView._21, g_FrameCB.InvView._31);
	upWS = float3(g_FrameCB.InvView._12, g_FrameCB.InvView._22, g_FrameCB.InvView._32);
	forwardWS = float3(g_FrameCB.InvView._13, g_FrameCB.InvView._23, g_FrameCB.InvView._33);

	rightWS = NormalizeSafe3(rightWS, float3(1, 0, 0));
	upWS = NormalizeSafe3(upWS, float3(0, 1, 0));
	forwardWS = NormalizeSafe3(forwardWS, float3(0, 0, 1));
}

float3 RotateAroundUp(float3 v, float3 upWS, float yaw)
{
	float s = sin(yaw);
	float c = cos(yaw);
	float3 a = upWS;
	return v * c + cross(a, v) * s + a * dot(a, v) * (1.0f - c);
}

BASE_VS_MAIN_ENTRY(InstanceID)
{
	uint baseInstance = g_SpeciesLodOffsets[g_DrawCB.StartInstanceLocation];
	uint globalInstanceId = InstanceID + baseInstance;

	GrassBillboardInstance rawInst = g_GrassInstances[globalInstanceId];

	float3 vertexPosition = GET_VERTEX_POS();
	float2 vertexUV = GET_VERTEX_UV();

	float3 posWS;
	float scale;
	float yaw;
	uint atlasIndex;
	uint seed8;
	DecodeGrassBillboardInstance(rawInst, posWS, scale, yaw, atlasIndex, seed8);

	float3 camR, camU, camF;
	GetCameraBasisWS(camR, camU, camF);

	camR = RotateAroundUp(camR, camU, yaw);
	float x = vertexPosition.x * scale;
	float y = vertexPosition.y * scale;

	float3 worldPos = posWS + camR * x + camU * y;
	float3 worldNormal = NormalizeSafe3(cross(camU, camR), float3(0, 0, 1));
	float3 worldTangent = NormalizeSafe3(camR, float3(1.0f, 0.0f, 0.0f));

	// Output
	SET_VSOUT_WORLD_POS_STATIC(float4(worldPos, 1.0f));
	SET_VSOUT_UV(vertexUV);
	SET_VSOUT_WORLD_NORMAL(worldNormal);
	SET_VSOUT_WORLD_TANGENT(worldTangent);
}
