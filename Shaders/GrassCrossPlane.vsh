#include "HLSL_Structures.hlsli"
#include "GrassCommon.hlsli"

cbuffer FRAME_CONSTANTS
{
	FrameConstants g_FrameCB;
};

cbuffer GRASS_RENDER_CONSTANTS
{
	GrassRenderConstants g_GrassCB;
};

StructuredBuffer<GrassCrossPlaneInstance> g_GrassInstances;

struct VSInput
{
	float3 Pos : ATTRIB0;
	float2 UV : ATTRIB1;
	float3 Normal : ATTRIB2;
	float3 Tangent : ATTRIB3;
};

struct VSOutput
{
	float4 Pos : SV_Position;
	float3 PosWS : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float2 UV : TEXCOORD2;
};

VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
{
	VSOutput OUT;

	GrassCrossPlaneInstance rawInst = g_GrassInstances[instanceID];

	float3 posWS;
	float scale;
	float yaw;
	float bend01;
	float press;
	uint variantId;
	uint seed8;
	uint atlasFrame;
	uint flags;

	DecodeGrassCrossPlaneInstance(
        rawInst,
        posWS,
        scale,
        yaw,
        bend01,
        press,
        variantId,
        seed8,
        atlasFrame,
        flags);

    // -----------------------------------------------------------------------
    // Local -> scale
    // NOTE: Cross-plane typically uses yaw only.
    // -----------------------------------------------------------------------
	float3 p = IN.Pos * scale;
	float3 n = IN.Normal;

    // -----------------------------------------------------------------------
    // Interaction
    // -----------------------------------------------------------------------
	float pressHard = smoothstep(0.05f, 0.25f, saturate(press));
	float keepBase = 1.0f - pressHard;

    // -----------------------------------------------------------------------
    // Base rigid orientation: yaw
    // -----------------------------------------------------------------------
	p = ApplyYaw(p, yaw);
	n = ApplyYaw(n, yaw);

    // -----------------------------------------------------------------------
    // Tip weight
    // NOTE: If plane mesh height is in UV, use UV.y instead.
    // -----------------------------------------------------------------------
	float height01 = saturate(IN.Pos.y);
	float wTip = height01 * height01;
	wTip = wTip * wTip;

    // -----------------------------------------------------------------------
    // Flatten axis
    // -----------------------------------------------------------------------
	float2 pressDir2 = float2(cos(yaw), sin(yaw));
	float3 pressDirWS = float3(pressDir2.x, 0.0f, pressDir2.y);

	float3 pressAxis = cross(float3(0.0f, 1.0f, 0.0f), pressDirWS);
	pressAxis = NormalizeSafe3(pressAxis, float3(1.0f, 0.0f, 0.0f));

    // -----------------------------------------------------------------------
    // Wind
    // -----------------------------------------------------------------------
	float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1.0f, 0.0f));
	float3 windDirWS = float3(windDir2.x, 0.0f, windDir2.y);

	static const float WIND_DIR_JITTER = 0.35f;

	float3 windDirJittered = ApplyYaw(windDirWS, (yaw - GRASS_PI) * WIND_DIR_JITTER);
	windDirJittered.y = 0.0f;
	windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

	float3 windBendAxis = cross(float3(0.0f, 1.0f, 0.0f), windDirJittered);
	windBendAxis = NormalizeSafe3(windBendAxis, float3(1.0f, 0.0f, 0.0f));

	float phase = dot(posWS.xz, windDir2) * g_GrassCB.WindFreq
                + g_FrameCB.CurrTime * g_GrassCB.WindSpeed
                + yaw * 0.37f;

	float gust = 1.0f + g_GrassCB.WindGust *
                 sin(g_FrameCB.CurrTime * (g_GrassCB.WindSpeed * 0.63f) + yaw);

	float windS = sin(phase);
	float windMag = windS * 0.5f + 0.5f;

	float windAngle = windMag * gust * bend01 * g_GrassCB.WindStrength;

	float windFade = saturate(g_GrassCB.InteractionWindFade);
	float windKeep = lerp(1.0f, 1.0f - windFade, pressHard);

	windKeep *= keepBase;

	windAngle *= windKeep;
	windAngle = clamp(windAngle, -g_GrassCB.MaxBendAngle, g_GrassCB.MaxBendAngle);

	p = RotateAroundAxis(p, windBendAxis, windAngle * wTip);
	n = RotateAroundAxis(n, windBendAxis, windAngle * wTip);

    // -----------------------------------------------------------------------
    // Flatten to ground when pressed
    // -----------------------------------------------------------------------
	float3 root = float3(0.0f, 0.0f, 0.0f);

	float targetFlat = max(g_GrassCB.InteractionBendAngle, 0.0f);
	float flattenAngle = targetFlat * pressHard;

	float3 local = p - root;
	local = RotateAroundAxis(local, pressAxis, flattenAngle * pressHard);
	p = local + root;

	n = RotateAroundAxis(n, pressAxis, flattenAngle * pressHard);

	p.y -= pressHard * g_GrassCB.InteractionSink;

    // -----------------------------------------------------------------------
    // World translate & output
    // -----------------------------------------------------------------------
	p += posWS;

	OUT.PosWS = p;
	OUT.NormalWS = NormalizeSafe3(n, float3(0.0f, 1.0f, 0.0f));
	OUT.UV = IN.UV;
	OUT.Pos = mul(float4(p, 1.0f), g_FrameCB.ViewProj);

	return OUT;
}
