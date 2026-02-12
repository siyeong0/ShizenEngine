#include "Common.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<GrassMeshInstance> g_GrassInstances;

BASE_VS_MAIN_ENTRY(InstanceID)
{
    GrassMeshInstance rawInst = g_GrassInstances[InstanceID];

    float3 vertexPosition = GET_VERTEX_POS();
    float2 vertexUV = GET_VERTEX_UV();
    float3 vertexNormal = GET_VERTEX_NORMAL();
    float3 vertexTangent = GET_VERTEX_TANGENT();

    float3 posWS;
    float scale;
    float yaw;
    float pitch;
    float bend01;
    float press;
    uint variantId;
    uint seed8;

    DecodeGrassMeshInstance(
        rawInst,
        posWS,
        scale,
        yaw,
        pitch,
        bend01,
        press,
        variantId,
        seed8);

    float3 p = vertexPosition * scale;
    float3 n = vertexNormal;
    float3 t = vertexTangent;

    float pressHard = smoothstep(0.05f, 0.25f, saturate(press));
    float keepBase = 1.0f - pressHard;

    p = ApplyYaw(p, yaw);
    n = ApplyYaw(n, yaw);
    t = ApplyYaw(t, yaw);

    float3 pitchAxis = ApplyYaw(float3(1.0f, 0.0f, 0.0f), yaw);
    pitchAxis = NormalizeSafe3(pitchAxis, float3(1.0f, 0.0f, 0.0f));

    float pitchAngle = pitch * keepBase;
    p = RotateAroundAxis(p, pitchAxis, pitchAngle);
    n = RotateAroundAxis(n, pitchAxis, pitchAngle);
    t = RotateAroundAxis(t, pitchAxis, pitchAngle);

    float height01 = saturate(vertexPosition.y);
    float wTip = height01 * height01;

    float2 pressDir2 = float2(cos(yaw), sin(yaw));
    float3 pressDirWS = float3(pressDir2.x, 0.0f, pressDir2.y);

    float3 pressAxis = cross(float3(0.0f, 1.0f, 0.0f), pressDirWS);
    pressAxis = NormalizeSafe3(pressAxis, float3(1.0f, 0.0f, 0.0f));

    float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1.0f, 0.0f));
    float3 windDirWS = float3(windDir2.x, 0.0f, windDir2.y);

    static const float WIND_DIR_JITTER = 0.35f;

    float3 windDirJittered = ApplyYaw(windDirWS, (yaw - GRASS_PI) * WIND_DIR_JITTER);
    windDirJittered.y = 0.0f;
    windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

    float3 windBendAxis = cross(float3(0.0f, 1.0f, 0.0f), windDirJittered);
    windBendAxis = NormalizeSafe3(windBendAxis, float3(1.0f, 0.0f, 0.0f));

    float phase =
        dot(posWS.xz, windDir2) * g_GrassCB.WindFreq +
        g_FrameCB.CurrTime * g_GrassCB.WindSpeed +
        yaw * 0.37f;

    float gust =
        1.0f +
        g_GrassCB.WindGust *
        sin(g_FrameCB.CurrTime * (g_GrassCB.WindSpeed * 0.63f) + yaw);

    float windMag = sin(phase) * 0.5f + 0.5f;

    float windAngle =
        windMag *
        gust *
        bend01 *
        g_GrassCB.WindStrength;

    float windKeep =
        (1.0f - pressHard * saturate(g_GrassCB.InteractionWindFade)) *
        keepBase;

    windAngle *= windKeep;
    windAngle = clamp(windAngle, -g_GrassCB.MaxBendAngle, g_GrassCB.MaxBendAngle);

    float windTipAngle = windAngle * wTip;

    p = RotateAroundAxis(p, windBendAxis, windTipAngle);
    n = RotateAroundAxis(n, windBendAxis, windTipAngle);
    t = RotateAroundAxis(t, windBendAxis, windTipAngle);

    float targetFlat = max(g_GrassCB.InteractionBendAngle, 0.0f);
    float flattenAngle = targetFlat * pressHard;

    p = RotateAroundAxis(p, pressAxis, flattenAngle * pressHard);
    n = RotateAroundAxis(n, pressAxis, flattenAngle * pressHard);
    t = RotateAroundAxis(t, pressAxis, flattenAngle * pressHard);

    p.y -= pressHard * g_GrassCB.InteractionSink;

    p += posWS;

    float3 normal = NormalizeSafe3(n, float3(0.0f, 1.0f, 0.0f));
    float3 tangent = NormalizeSafe3(t, float3(1.0f, 0.0f, 0.0f));

    SET_VSOUT_WORLD_POS_STATIC(float4(p, 1.0f));
    SET_VSOUT_UV(vertexUV);
    SET_VSOUT_WORLD_NORMAL(normal);
    SET_VSOUT_WORLD_TANGENT(tangent);
}
