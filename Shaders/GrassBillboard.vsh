#include "HLSL_Structures.hlsli"
#include "GrassCommon.hlsli"

cbuffer FRAME_CONSTANTS
{
	FrameConstants g_FrameCB;
};

StructuredBuffer<GrassBillboardInstance> g_GrassInstances;

struct VSInput
{
	float3 Pos : ATTRIB0; // local quad corner: x [-0.5..0.5], y [0..1], z 0
	float2 UV : ATTRIB1;
};

struct VSOutput
{
	float4 Pos : SV_Position;
	float3 PosWS : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float2 UV : TEXCOORD2;
};

void GetCameraBasisWS(out float3 rightWS, out float3 upWS, out float3 forwardWS)
{
	rightWS = float3(g_FrameCB.View._11, g_FrameCB.View._21, g_FrameCB.View._31);
	upWS = float3(g_FrameCB.View._12, g_FrameCB.View._22, g_FrameCB.View._32);
	forwardWS = float3(g_FrameCB.View._13, g_FrameCB.View._23, g_FrameCB.View._33);

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

VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
{
	VSOutput OUT;

	GrassBillboardInstance rawInst = g_GrassInstances[instanceID];

	float3 posWS;
	float scale;
	float yaw;
	uint atlasIndex;
	uint seed8;

	DecodeGrassBillboardInstance(rawInst, posWS, scale, yaw, atlasIndex, seed8);

	float3 camR;
	float3 camU;
	float3 camF;
	GetCameraBasisWS(camR, camU, camF);

    // Rotate billboard around camera up for per-instance variation.
	// camR = RotateAroundUp(camR, camU, yaw);

	float x = IN.Pos.x * scale;
	float y = IN.Pos.y * scale;

	float3 worldPos = posWS + camR * x + camU * y;

	float3 N = NormalizeSafe3(-camF, float3(0.0f, 0.0f, 1.0f));

	OUT.PosWS = worldPos;
	OUT.NormalWS = N;
	OUT.UV = IN.UV;
	OUT.Pos = mul(float4(worldPos, 1.0f), g_FrameCB.ViewProj);

	return OUT;
}
