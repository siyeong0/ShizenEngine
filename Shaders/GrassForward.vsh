#include "HLSL_Structures.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer GRASS_RENDER_CONSTANTS
{
    GrassRenderConstants g_GrassCB;
};

StructuredBuffer<GrassInstance> g_GrassInstances;

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

static const float EPS = 1e-8;
static const float PI = 3.14159265;

float3 ApplyYaw(float3 v, float yaw)
{
    const float s = sin(yaw);
    const float c = cos(yaw);
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

//VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
//{
//    VSOutput OUT;

//    GrassInstance inst = g_GrassInstances[instanceID];

//    // ------------------------------------------------------------
//    // Local -> Scale
//    // ------------------------------------------------------------
//    float3 p = IN.Pos * inst.Scale;
//    float3 n = IN.Normal;

//    // ------------------------------------------------------------
//    // Base rigid orientation (Yaw + Pitch)
//    // ------------------------------------------------------------
//    p = ApplyYaw(p, inst.Yaw);
//    n = ApplyYaw(n, inst.Yaw);

//    float3 pitchAxis = ApplyYaw(float3(1, 0, 0), inst.Yaw);
//    pitchAxis = NormalizeSafe3(pitchAxis, float3(1, 0, 0));

//    p = RotateAroundAxis(p, pitchAxis, inst.Pitch);
//    n = RotateAroundAxis(n, pitchAxis, inst.Pitch);

//    // ------------------------------------------------------------
//    // Height weight (tip moves more)
//    // NOTE: if IN.Pos.y is not 0..1 along blade height, use IN.UV.y instead.
//    // ------------------------------------------------------------
//    float height01 = saturate(IN.Pos.y);
//    // float height01 = saturate(IN.UV.y); // alternative
//    float wTip = height01 * height01;
//    wTip = wTip * wTip; // h^4

//    // A second weight used for "full flatten" that should affect almost the whole blade.
//    // This keeps the root more stable but allows the entire blade to go down when pressed.
//    float wBlade = saturate(height01 * 1.35f); // more coverage than tip-only
//    wBlade = wBlade * wBlade; // h^2

//    // ------------------------------------------------------------
//    // Interaction 
//    // ------------------------------------------------------------
//    float press = saturate(inst.Press);
//    float pressHard = smoothstep(0.05, 0.25, press);

//    // Direction to flatten: cheap and stable (per-instance yaw).
//    // If you want "push direction" instead, feed it from stamps and store in instance.
//    float2 pressDir2 = float2(cos(inst.Yaw), sin(inst.Yaw));
//    float3 pressDirWS = float3(pressDir2.x, 0.0, pressDir2.y);

//    float3 pressAxis = cross(float3(0, 1, 0), pressDirWS);
//    pressAxis = NormalizeSafe3(pressAxis, float3(1, 0, 0));

//    // ------------------------------------------------------------
//    // Wind (pressed grass reduces wind response)
//    // ------------------------------------------------------------
//    float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1, 0));
//    float3 windDirWS = float3(windDir2.x, 0.0, windDir2.y);

//    // Slight per-instance direction variation
//    static const float WIND_DIR_JITTER = 0.35f;
//    float3 windDirJittered = ApplyYaw(windDirWS, (inst.Yaw - PI) * WIND_DIR_JITTER);
//    windDirJittered.y = 0.0;
//    windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

//    float3 windBendAxis = cross(float3(0, 1, 0), windDirJittered);
//    windBendAxis = NormalizeSafe3(windBendAxis, float3(1, 0, 0));

//    float phase = dot(inst.PosWS.xz, windDir2) * g_GrassCB.WindFreq
//                + g_FrameCB.CurrTime * g_GrassCB.WindSpeed;
//    phase += inst.Yaw * 0.37f;

//    // Optional gust
//    float gust = 1.0f + g_GrassCB.WindGust * sin(g_FrameCB.CurrTime * (g_GrassCB.WindSpeed * 0.63f) + inst.Yaw);

//    float windS = sin(phase);
//    float windMag = windS * 0.5f + 0.5f; // 0..1
//    float windAngle = windMag * gust * inst.BendStrength * g_GrassCB.WindStrength;

//    // IMPORTANT: apply wind fade by press (WindFade=1 => pressed grass almost no wind)
//    float windFade = saturate(g_GrassCB.InteractionWindFade); // 0..1
//    float windKeep = lerp(1.0f, 1.0f - windFade, pressHard); // pressHard=1 => 1-windFade
//    windAngle *= windKeep;

//    windAngle = clamp(windAngle, -g_GrassCB.MaxBendAngle, g_GrassCB.MaxBendAngle);
    
//    // Apply wind first (tip-weighted)
//    p = RotateAroundAxis(p, windBendAxis, windAngle * wTip);
//    n = RotateAroundAxis(n, windBendAxis, windAngle * wTip);

//    // ------------------------------------------------------------
//    // Full flatten to ground when pressed (wins over wind)
//    // ------------------------------------------------------------
//    // Pivot at blade root (assumes mesh root at y=0; adjust if your mesh pivot differs).
//    float3 root = float3(0.0f, 0.0f, 0.0f);

//    // Target "almost flat" angle (~89 degrees). Press=1 => near ground.
//    float targetFlat = 1.553343f; // radians (~89 deg)
//    float flattenAngle = targetFlat * pressHard;

//    // Flatten affects most of blade, not only the tip.
//    float flattenW = pressHard * wBlade;

//    float3 local = p - root;
//    local = RotateAroundAxis(local, pressAxis, flattenAngle * flattenW);
//    p = local + root;

//    n = RotateAroundAxis(n, pressAxis, flattenAngle * flattenW);

//    // Extra sink so it "sticks" to ground when fully pressed
//    p.y -= pressHard * g_GrassCB.InteractionSink * wBlade;

//    // ------------------------------------------------------------
//    // World translate & output
//    // ------------------------------------------------------------
//    p += inst.PosWS;

//    OUT.PosWS = p;
//    OUT.NormalWS = NormalizeSafe3(n, float3(0, 1, 0));
//    OUT.UV = IN.UV;
//    OUT.Pos = mul(float4(p, 1.0f), g_FrameCB.ViewProj);

//    return OUT;
//}



VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
{
    VSOutput OUT;

    GrassInstance inst = g_GrassInstances[instanceID];

    // ------------------------------------------------------------
    // Local -> Scale
    // ------------------------------------------------------------
    float3 p = IN.Pos * inst.Scale;
    float3 n = IN.Normal;

    // ------------------------------------------------------------
    // Interaction (compute early for blending)
    // ------------------------------------------------------------
    float press = saturate(inst.Press);
    float pressHard = smoothstep(0.05, 0.25, press); // 0..1
    float keepBase = 1.0f - pressHard; // press=1 => base motion removed

    // ------------------------------------------------------------
    // Base rigid orientation: Yaw (always) + Pitch (fades out when pressed)
    // ------------------------------------------------------------
    p = ApplyYaw(p, inst.Yaw);
    n = ApplyYaw(n, inst.Yaw);

    float3 pitchAxis = ApplyYaw(float3(1, 0, 0), inst.Yaw);
    pitchAxis = NormalizeSafe3(pitchAxis, float3(1, 0, 0));

    float pitchAngle = inst.Pitch * keepBase; // ¡Ú ÇÙ½É: Press=1ÀÌ¸é Pitch 0
    p = RotateAroundAxis(p, pitchAxis, pitchAngle);
    n = RotateAroundAxis(n, pitchAxis, pitchAngle);

    // ------------------------------------------------------------
    // Height weight (tip moves more)
    // NOTE: if IN.Pos.y is not 0..1 along blade height, use IN.UV.y instead.
    // ------------------------------------------------------------
    float height01 = saturate(IN.Pos.y);
    float wTip = height01 * height01;
    wTip = wTip * wTip; // h^4

    // ------------------------------------------------------------
    // Flatten direction (per-instance cheap dir)
    // ------------------------------------------------------------
    float2 pressDir2 = float2(cos(inst.Yaw), sin(inst.Yaw));
    float3 pressDirWS = float3(pressDir2.x, 0.0, pressDir2.y);

    float3 pressAxis = cross(float3(0, 1, 0), pressDirWS);
    pressAxis = NormalizeSafe3(pressAxis, float3(1, 0, 0));

    // ------------------------------------------------------------
    // Wind (pressed grass reduces wind response)
    // ------------------------------------------------------------
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
    float windMag = windS * 0.5f + 0.5f; // 0..1
    float windAngle = windMag * gust * inst.BendStrength * g_GrassCB.WindStrength;

    // Existing control: InteractionWindFade (0..1)
    float windFade = saturate(g_GrassCB.InteractionWindFade);
    float windKeep = lerp(1.0f, 1.0f - windFade, pressHard);

    windKeep *= keepBase;

    windAngle *= windKeep;
    windAngle = clamp(windAngle, -g_GrassCB.MaxBendAngle, g_GrassCB.MaxBendAngle);

    // Apply wind first (tip-weighted)
    p = RotateAroundAxis(p, windBendAxis, windAngle * wTip);
    n = RotateAroundAxis(n, windBendAxis, windAngle * wTip);

    // ------------------------------------------------------------
    // Full flatten to ground when pressed (final pose override)
    // ------------------------------------------------------------
    float3 root = float3(0.0f, 0.0f, 0.0f);

    float targetFlat = 1.2f; // ~70 deg
    float flattenAngle = targetFlat * pressHard; // press=1 => near flat
    float flattenW = pressHard; // press=1 => whole blade

    float3 local = p - root;
    local = RotateAroundAxis(local, pressAxis, flattenAngle * flattenW);
    p = local + root;

    n = RotateAroundAxis(n, pressAxis, flattenAngle * flattenW);

    // Stick to ground when pressed
    p.y -= pressHard * g_GrassCB.InteractionSink;

    // ------------------------------------------------------------
    // World translate & output
    // ------------------------------------------------------------
    p += inst.PosWS;

    OUT.PosWS = p;
    OUT.NormalWS = NormalizeSafe3(n, float3(0, 1, 0));
    OUT.UV = IN.UV;
    OUT.Pos = mul(float4(p, 1.0f), g_FrameCB.ViewProj);

    return OUT;
}
