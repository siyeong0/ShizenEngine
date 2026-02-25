#define ADDITIONAL_VS_OUT_FIELDS \
	VS_OUT_FIELD(nointerpolation float3, ViewDirOS, 0) 

#include "Common.hlsli"
#include "ImpostorCommon.hlsli"

void GetCameraBasisWS(out float3 rightWS, out float3 upWS, out float3 forwardWS)
{
	rightWS = normalize(g_FrameCB.InvView[0].xyz);
	upWS = normalize(g_FrameCB.InvView[1].xyz);
	forwardWS = normalize(g_FrameCB.InvView[2].xyz);
}

void GetObjectAxesWS(out float3 ax, out float3 ay, out float3 az, float4x4 world)
{
	ax = normalize(world[0].xyz);
	ay = normalize(world[1].xyz);
	az = normalize(world[2].xyz);
}

BASE_VS_MAIN_ENTRY(InstanceID)
{
	ObjectConstants oc = g_ObjectTable[g_DrawCB.StartInstanceLocation + InstanceID];

	float3 centerWS = oc.World[3].xyz;
	
	float3 ax = normalize(oc.World[0].xyz);
	float3 ay = normalize(oc.World[1].xyz);
	float3 az = normalize(oc.World[2].xyz);

	float3 camR = normalize(g_FrameCB.InvView[0].xyz);
	float3 camU = normalize(g_FrameCB.InvView[1].xyz);
	float3 camF = normalize(g_FrameCB.InvView[2].xyz);

	float3 scaleWS = float3(length(oc.World[0].xyz), length(oc.World[1].xyz), length(oc.World[2].xyz));

	float3 vertexPosition = GET_VERTEX_POS();
	float3 billboardWS = centerWS
                       + camR * (vertexPosition.x * scaleWS.x)
                       + camU * (vertexPosition.y * scaleWS.y);

	float3 viewDirWS = normalize(g_FrameCB.CameraPosition - centerWS);
	float3 viewDirOS = float3(
		dot(viewDirWS, ax),
		dot(viewDirWS, ay),
		dot(viewDirWS, az)
	);

	SET_VSOUT_WORLD_POS_STATIC(float4(billboardWS, 1.0f));
	SET_VSOUT_UV(GET_VERTEX_UV());
	SET_VSOUT_WORLD_NORMAL(-camF);
	SET_VSOUT_WORLD_TANGENT(camR);

	SET_VSOUT_ADDITIONAL(ViewDirOS, normalize(viewDirOS));
}