#include "Common.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<uint> g_SpeciesLodOffsets;

StructuredBuffer<GrassCrossPlaneInstance> g_GrassInstances;

BASE_VS_MAIN_ENTRY(InstanceID)
{
	uint baseInstance = g_SpeciesLodOffsets[g_DrawCB.StartInstanceLocation];
	uint globalInstanceId = InstanceID + baseInstance;

	GrassCrossPlaneInstance rawInst = g_GrassInstances[globalInstanceId];

	float3 vertexPosition = GET_VERTEX_POS();
	float2 vertexUV = GET_VERTEX_UV();
	float3 vertexNormal = GET_VERTEX_NORMAL();
	float3 vertexTangent = GET_VERTEX_TANGENT();

	float3 posWS;
	float scale;
	float yaw;
	float bend01;
	float press;
	uint variantId;
	uint seed8;
	uint atlasFrame;
	uint flags;

	DecodeGrassCrossPlaneInstance(rawInst, posWS, scale, yaw, bend01, press, variantId, seed8, atlasFrame, flags);

	float3 p = vertexPosition * scale;
	float3 n = vertexNormal;
	float3 t = vertexTangent;

	float pressHard = smoothstep(0.05f, 0.25f, saturate(press));
	float keepBase = 1.0f - pressHard;

	// Yaw only (geometry orientation)
	p = ApplyYaw(p, yaw);
	n = ApplyYaw(n, yaw);
	t = ApplyYaw(t, yaw);

	// Tip weight (drive wind/interaction to affect tip more than root)
	float height01 = saturate(vertexPosition.y);
	float wTip = height01 * height01;
	wTip = wTip * wTip;

	// Flatten axis (interaction direction)
	float2 pressDir2 = float2(cos(yaw), sin(yaw));
	float3 pressDirWS = float3(pressDir2.x, 0.0f, pressDir2.y);

	float3 pressAxis = cross(float3(0.0f, 1.0f, 0.0f), pressDirWS);
	pressAxis = NormalizeSafe3(pressAxis, float3(1.0f, 0.0f, 0.0f));

	// Wind (UV only)
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

	// -------------------------------------------------------------------------
	// Flatten to ground (interaction) - affects geometry, NOT UV
	// -------------------------------------------------------------------------
	float3 root = float3(0.0f, 0.0f, 0.0f);

	float targetFlat = max(g_GrassCB.InteractionBendAngle, 0.0f);
	float flattenAngle = targetFlat * pressHard;

	float3 local = p - root;
	local = RotateAroundAxis(local, pressAxis, flattenAngle * pressHard);
	p = local + root;

	n = RotateAroundAxis(n, pressAxis, flattenAngle * pressHard);
	t = RotateAroundAxis(t, pressAxis, flattenAngle * pressHard);

	p.y -= pressHard * g_GrassCB.InteractionSink;

	// World translate & output
	p += posWS;

	float3 worldNormal = NormalizeSafe3(n, float3(0.0f, 1.0f, 0.0f));
	float3 worldTangent = NormalizeSafe3(t, float3(1.0f, 0.0f, 0.0f));

	SET_VSOUT_WORLD_POS_STATIC(float4(p, 1.0f));
	SET_VSOUT_UV(vertexUV);
	SET_VSOUT_WORLD_NORMAL(worldNormal);
	SET_VSOUT_WORLD_TANGENT(worldTangent);
}
