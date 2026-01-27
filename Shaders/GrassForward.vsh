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

static const float kEps = 1e-8;
static const float kPI = 3.14159265;

float3 ApplyYaw(float3 v, float yaw)
{
    const float s = sin(yaw);
    const float c = cos(yaw);
    const float x = c * v.x - s * v.z;
    const float z = s * v.x + c * v.z;
    return float3(x, v.y, z);
}

// Rodrigues (axis must be unit)
float3 RotateAroundAxis(float3 v, float3 axisUnit, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return v * c + cross(axisUnit, v) * s + axisUnit * dot(axisUnit, v) * (1.0 - c);
}

float3 NormalizeSafe3(float3 v, float3 fallback)
{
    float len2 = dot(v, v);
    if (len2 < kEps)
        return fallback;
    return v * rsqrt(len2);
}

float2 NormalizeSafe2(float2 v, float2 fallback)
{
    float len2 = dot(v, v);
    if (len2 < kEps)
        return fallback;
    return v * rsqrt(len2);
}

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
    // 1) Base rigid orientation (Yaw + Pitch)
    //    - Yaw: around Y
    //    - Pitch: around local X after yaw (consistent lean direction)
    // ------------------------------------------------------------
    p = ApplyYaw(p, inst.Yaw);
    n = ApplyYaw(n, inst.Yaw);

    float3 pitchAxis = ApplyYaw(float3(1, 0, 0), inst.Yaw);
    pitchAxis = NormalizeSafe3(pitchAxis, float3(1, 0, 0));

    // base pitch is a rigid tilt (NOT height-weighted)
    p = RotateAroundAxis(p, pitchAxis, inst.Pitch);
    n = RotateAroundAxis(n, pitchAxis, inst.Pitch);

    // ------------------------------------------------------------
    // Height weight (tip moves more)
    // NOTE: If your mesh has messy IN.Pos.y distribution, switch to IN.UV.y.
    // ------------------------------------------------------------
    float height01 = saturate(IN.Pos.y);
    float w = height01 * height01;
    w = w * w; // h^4 tip emphasis

    // ------------------------------------------------------------
    // 2) Wind direction in world (XZ)
    // ------------------------------------------------------------
    float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1, 0));
    float3 windDirWS = float3(windDir2.x, 0.0, windDir2.y);

    // Optional: per-instance directional variation (jitter around global wind)
    float dirJitter = 0.35; // 0 = all same, 1 = strong variation
    float3 windDirJittered = ApplyYaw(windDirWS, (inst.Yaw - kPI) * dirJitter);
    windDirJittered.y = 0.0;
    windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

    // Wind bend axis: rotate towards wind direction
    // axis = Up x WindDir
    float3 windBendAxis = cross(float3(0, 1, 0), windDirJittered);
    windBendAxis = NormalizeSafe3(windBendAxis, float3(1, 0, 0));

    // ------------------------------------------------------------
    // 3) Wind signal (instance coherent)
    // ------------------------------------------------------------
    float phase =
        dot(inst.PosWS.xz, windDir2) * g_GrassCB.WindFreq +
        g_FrameCB.CurrTime * g_GrassCB.WindSpeed;

    // per-instance phase offset (avoid perfect sync)
    phase += inst.Yaw * 0.37;

    float windS = sin(phase);
    float windMag = windS * 0.5 + 0.5; // 0..1

    float windAngle = windMag * inst.BendStrength * g_GrassCB.WindStrength;

    // ------------------------------------------------------------
    // 4) Directional response vs base "facing/lean" direction
    //    - If wind comes from the opposite side of the blade's facing direction, bend less.
    //
    // We compute a stable blade facing direction in world:
    //   bladeForwardLS = (0,0,1)  (change if your mesh uses +X as forward)
    //   apply yaw, then base pitch -> forwardWS
    //   project to XZ -> facingDirXZ
    // ------------------------------------------------------------
    // Lean direction from base pitch (what direction the blade is already leaning towards)
    float3 upWS = float3(0, 1, 0);
    float3 leanedUpWS = RotateAroundAxis(upWS, pitchAxis, inst.Pitch);

    leanedUpWS.y = 0.0;
    float3 leanDirWS = NormalizeSafe3(leanedUpWS, windDirJittered);

    float align = dot(leanDirWS, windDirJittered); // [-1, 1]
    
    float minResponse = 0.1;
    float t = saturate(0.5 * (align + 1.0));
    t = t * t; // sharpen
    float response = lerp(minResponse, 1.0, t);
    windAngle *= response;

    // Clamp after response (more intuitive)
    windAngle = clamp(windAngle, -g_GrassCB.MaxBendAngle, g_GrassCB.MaxBendAngle);

    // ------------------------------------------------------------
    // 5) Apply wind bend around base pivot (height-weighted)
    // ------------------------------------------------------------
    float3 pivot = float3(0, 0, 0);
    p = RotateAroundAxis(p - pivot, windBendAxis, windAngle * w) + pivot;
    n = RotateAroundAxis(n, windBendAxis, windAngle * w);

    // ------------------------------------------------------------
    // World translate & output
    // ------------------------------------------------------------
    p += inst.PosWS;

    OUT.PosWS = p;
    OUT.NormalWS = NormalizeSafe3(n, float3(0, 1, 0));
    OUT.UV = IN.UV;

    OUT.Pos = mul(float4(p, 1.0), g_FrameCB.ViewProj);
    return OUT;
}
