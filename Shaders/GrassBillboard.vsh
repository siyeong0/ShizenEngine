#include "Common.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<uint> g_SpeciesLodOffsets;

StructuredBuffer<GrassBillboardInstance> g_GrassInstances;

void GetCameraBasisWS(out float3 rightWS, out float3 upWS, out float3 forwardWS)
{
	rightWS = float3(g_ViewCB.View._11, g_ViewCB.View._21, g_ViewCB.View._31);
	upWS = float3(g_ViewCB.View._12, g_ViewCB.View._22, g_ViewCB.View._32);
	forwardWS = float3(g_ViewCB.View._13, g_ViewCB.View._23, g_ViewCB.View._33);

	rightWS = NormalizeSafe3(rightWS, float3(1.0f, 0.0f, 0.0f));
	upWS = NormalizeSafe3(upWS, float3(0.0f, 1.0f, 0.0f));
	forwardWS = NormalizeSafe3(forwardWS, float3(0.0f, 0.0f, 1.0f));
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

    // Optional: per-instance yaw around camera up
    // camR = RotateAroundUp(camR, camU, yaw);

	float x = vertexPosition.x * scale;
	float y = vertexPosition.y * scale;

	float3 worldPos = posWS + camR * x + camU * y;

    // Billboard normal: face camera (or -camF depending on convention)
	float3 worldNormal = NormalizeSafe3(-camF, float3(0.0f, 0.0f, 1.0f));

    // Tangent: use camera right as stable tangent
	float3 worldTangent = NormalizeSafe3(camR, float3(1.0f, 0.0f, 0.0f));

    // -------------------------------------------------------------------------
    // Wind (UV only) via ApplyGrassWindUV from GrassCommon.hlsli
    // -------------------------------------------------------------------------
    // Billboard has no per-vertex bending; emulate tip weighting using vertexPosition.y.
    // Assumption: vertexPosition.y is 0..1 (bottom->top) in billboard local space.
	float height01 = saturate(vertexPosition.y);

	float wTip = height01 * height01;
	wTip = wTip * wTip;

    // Billboard doesn't have interaction press or bend01; use stable defaults.
    // - bend01: treat billboard as fully bendable (1.0)
    // - pressHard: no interaction flatten in billboard path (0.0)
    // - keepBase: 1.0 means wind is allowed on base; wTip already keeps base steady
	float bend01 = 1.0f;
	float pressHard = 0.0f;
	float keepBase = 1.0f;

	vertexUV = ApplyGrassWindUV(
        vertexUV,
        posWS,
        scale,
        yaw,
        bend01,
        pressHard,
        keepBase,
        wTip,
        seed8);

    // Output
    SET_VSOUT_WORLD_POS_STATIC(float4(worldPos, 1.0f));
    SET_VSOUT_UV(vertexUV);
    SET_VSOUT_WORLD_NORMAL(worldNormal);
    SET_VSOUT_WORLD_TANGENT(worldTangent);
}
