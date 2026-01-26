#include "HLSL_Structures.hlsli"

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

VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
{
    VSOutput OUT;

    GrassInstance inst = g_GrassInstances[instanceID];

    // ----------------------------
    // Position: local -> world
    // ----------------------------
    float3 p = IN.Pos;
    p = ApplyYaw(p, inst.Yaw);
    p *= inst.Scale;
    p += inst.PosWS;

    OUT.PosWS = p;

    // Clip
    float4 worldPos4 = float4(p, 1.0);
    OUT.Pos = mul(worldPos4, g_FrameCB.ViewProj);
    OUT.Pos.y *= -1.0;
    
    float3 n = IN.Normal;

    // inverse scale (avoid div by zero)
    float invScale = 1.0 / inst.Scale;

    // Apply normal matrix approx for (Yaw * Scale):
    // normal' = normalize( R * (n * invScale) )
    n *= invScale;
    n = ApplyYaw(n, inst.Yaw);
    OUT.NormalWS = normalize(n);

    OUT.UV = IN.UV;

    return OUT;
}
