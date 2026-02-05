#include "HLSL_Structures.hlsli"

cbuffer SHADOW_CONSTANTS
{
    ShadowConstants g_ShadowCB;
};

cbuffer GRASS_RENDER_CONSTANTS
{
    GrassRenderConstants g_GrassCB;
};

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

StructuredBuffer<GrassInstance> g_GrassInstances;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
    float3 Normal : ATTRIB2;
    float3 Tangent : ATTRIB3;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

static const float EPS = 1e-8;
static const float PI = 3.14159265;

float3 ApplyYaw(float3 v, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);
    return float3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
}

float3 RotateAroundAxis(float3 v, float3 axisUnit, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return v * c + cross(axisUnit, v) * s + axisUnit * dot(axisUnit, v) * (1.0 - c);
}

float3 NormalizeSafe3(float3 v, float3 fallback)
{
    float len2 = dot(v, v);
    if (len2 < EPS)
        return fallback;
    return v * rsqrt(len2);
}

float2 NormalizeSafe2(float2 v, float2 fallback)
{
    float len2 = dot(v, v);
    if (len2 < EPS)
        return fallback;
    return v * rsqrt(len2);
}

void main(in VSInput IN, out PSInput OUT, uint instanceID : SV_InstanceID)
{
    GrassInstance inst = g_GrassInstances[instanceID];

    // Local -> Scale
    float3 p = IN.Pos * inst.Scale;
    float3 n = IN.Normal;

    // Interaction
    float press = saturate(inst.Press);
    float pressHard = smoothstep(0.05, 0.25, press);
    float keepBase = 1.0f - pressHard;

    // Base yaw + pitch (fade)
    p = ApplyYaw(p, inst.Yaw);
    n = ApplyYaw(n, inst.Yaw);

    float3 pitchAxis = ApplyYaw(float3(1, 0, 0), inst.Yaw);
    pitchAxis = NormalizeSafe3(pitchAxis, float3(1, 0, 0));

    float pitchAngle = inst.Pitch * keepBase;
    p = RotateAroundAxis(p, pitchAxis, pitchAngle);
    n = RotateAroundAxis(n, pitchAxis, pitchAngle);

    // Tip weight
    float height01 = saturate(IN.Pos.y);
    float wTip = height01 * height01;
    wTip = wTip * wTip;

    // Flatten axis
    float2 pressDir2 = float2(cos(inst.Yaw), sin(inst.Yaw));
    float3 pressDirWS = float3(pressDir2.x, 0.0, pressDir2.y);
    float3 pressAxis = cross(float3(0, 1, 0), pressDirWS);
    pressAxis = NormalizeSafe3(pressAxis, float3(1, 0, 0));

    // Wind
    float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1, 0));
    float3 windDirWS = float3(windDir2.x, 0.0, windDir2.y);

    static const float WIND_DIR_JITTER = 0.35f;
    float3 windDirJittered = ApplyYaw(windDirWS, (inst.Yaw - PI) * WIND_DIR_JITTER);
    windDirJittered.y = 0.0;
    windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

    float3 windBendAxis = cross(float3(0, 1, 0), windDirJittered);
    windBendAxis = NormalizeSafe3(windBendAxis, float3(1, 0, 0));

    float phase = dot(inst.PosWS.xz, windDir2) * g_GrassCB.WindFreq
                + g_FrameCB.CurrTime * g_GrassCB.WindSpeed
                + inst.Yaw * 0.37f;

    float gust = 1.0f + g_GrassCB.WindGust *
                 sin(g_FrameCB.CurrTime * (g_GrassCB.WindSpeed * 0.63f) + inst.Yaw);

    float windS = sin(phase);
    float windMag = windS * 0.5f + 0.5f;
    float windAngle = windMag * gust * inst.BendStrength * g_GrassCB.WindStrength;

    float windFade = saturate(g_GrassCB.InteractionWindFade);
    float windKeep = lerp(1.0f, 1.0f - windFade, pressHard);
    windKeep *= keepBase;

    windAngle *= windKeep;
    windAngle = clamp(windAngle, -g_GrassCB.MaxBendAngle, g_GrassCB.MaxBendAngle);

    p = RotateAroundAxis(p, windBendAxis, windAngle * wTip);

    // Flatten
    float3 root = float3(0, 0, 0);
    float targetFlat = 1.2f;
    float flattenAngle = targetFlat * pressHard;

    float3 local = p - root;
    local = RotateAroundAxis(local, pressAxis, flattenAngle);
    p = local + root;

    p.y -= pressHard * g_GrassCB.InteractionSink;

    // World translate
    p += inst.PosWS;

    // Shadow clip
    OUT.Pos = mul(float4(p, 1.0f), g_ShadowCB.LightViewProj);
    OUT.UV = IN.UV;
}
