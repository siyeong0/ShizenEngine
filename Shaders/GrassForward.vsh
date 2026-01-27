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

float3 ApplyYaw(float3 v, float yaw)
{
    const float s = sin(yaw);
    const float c = cos(yaw);

    const float x = c * v.x - s * v.z;
    const float z = s * v.x + c * v.z;
    return float3(x, v.y, z);
}

// Rotate vector around unit axis (Rodrigues)
float3 RotateAroundAxis(float3 v, float3 axisUnit, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return v * c + cross(axisUnit, v) * s + axisUnit * dot(axisUnit, v) * (1.0 - c);
}

VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
{
    VSOutput OUT;

    GrassInstance inst = g_GrassInstances[instanceID];

    // --------------------------------
    // Local -> Scale
    // --------------------------------
    float3 p = IN.Pos * inst.Scale;
    float3 n = IN.Normal;

    // --------------------------------
    // Yaw rotation
    // --------------------------------
    p = ApplyYaw(p, inst.Yaw);
    n = ApplyYaw(n, inst.Yaw);

    // --------------------------------
    // Height-based weight (smooth)
    // Assumption: blade base at y=0, tip around y=1 in the source mesh
    // If your mesh is taller/shorter, this still works because we clamp
    // --------------------------------
    float height01 = saturate(IN.Pos.y);
    float w = height01 * height01; // smoother than linear -> reduces "kinks"

    // --------------------------------
    // Wind (instance-coherent)
    // --------------------------------
    float2 windDir2 = normalize(g_GrassCB.WindDirXZ);
    float phase =
        dot(inst.PosWS.xz, windDir2) * g_GrassCB.WindFreq +
        g_FrameCB.CurrTime * g_GrassCB.WindSpeed;

    float wind = sin(phase);

    // --------------------------------
    // Total bend angle
    // --------------------------------
    float totalPitch =
        inst.Pitch +
        wind * inst.BendStrength * g_GrassCB.WindStrength;

    float angle = clamp(totalPitch * w,
                        -g_GrassCB.MaxBendAngle,
                         g_GrassCB.MaxBendAngle);

    // --------------------------------
    // Bend pivot: rotate around blade base (root)
    // IMPORTANT: this prevents zig-zag when mesh origin isn't at the base.
    //
    // If your mesh base is not exactly y=0, you can also set this to a constant
    // known base offset, but using (0,0,0) is safe when the blade is authored at y=0.
    // --------------------------------
    float3 pivot = float3(0, 0, 0);

    // Bend axis: local X after yaw (as you had)
    float3 bendAxis = float3(1, 0, 0);

    // Pivoted rotation
    p = RotateAroundAxis(p - pivot, bendAxis, angle) + pivot;
    n = RotateAroundAxis(n, bendAxis, angle);

    // --------------------------------
    // World translate
    // --------------------------------
    p += inst.PosWS;

    OUT.PosWS = p;
    OUT.NormalWS = normalize(n);
    OUT.UV = IN.UV;

    // Clip
    OUT.Pos = mul(float4(p, 1.0), g_FrameCB.ViewProj);

    return OUT;
}
