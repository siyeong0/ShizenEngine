#include "HLSL_Structures.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

StructuredBuffer<GrassInstance> g_GrassInstances;

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

static const float EPS = 1e-8;

float3 NormalizeSafe3(float3 v, float3 fallback)
{
    float len2 = dot(v, v);
    if (len2 < EPS)
        return fallback;
    return v * rsqrt(len2);
}

// NOTE: This follows your convention: OUT.Pos = mul(float4(p,1), ViewProj).
// If billboard faces wrong, adjust row/col choice or negate forward.
void GetCameraBasisWS(out float3 rightWS, out float3 upWS, out float3 forwardWS)
{
    rightWS = float3(g_FrameCB.View._11, g_FrameCB.View._21, g_FrameCB.View._31);
    upWS = float3(g_FrameCB.View._12, g_FrameCB.View._22, g_FrameCB.View._32);
    forwardWS = float3(g_FrameCB.View._13, g_FrameCB.View._23, g_FrameCB.View._33);

    rightWS = NormalizeSafe3(rightWS, float3(1, 0, 0));
    upWS = NormalizeSafe3(upWS, float3(0, 1, 0));
    forwardWS = NormalizeSafe3(forwardWS, float3(0, 0, 1));
}

float3 ApplyYaw(float3 v, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);
    return float3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
}

VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
{
    VSOutput OUT;

    GrassInstance inst = g_GrassInstances[instanceID];

    float3 camR, camU, camF;
    GetCameraBasisWS(camR, camU, camF);

    // Local quad axes (object space): width along X, height along Y
    float x = IN.Pos.x * inst.Scale;
    float y = IN.Pos.y * inst.Scale;

    // Optional: rotate quad around its normal by inst.Yaw (adds variety)
    float3 local = float3(x, y, 0.0);
    local = ApplyYaw(local, inst.Yaw);
    x = local.x;
    y = local.y;

    // Build camera-facing billboard in world
    float3 worldPos = inst.PosWS + camR * x + camU * y;

    // Billboard normal faces camera (approx)
    float3 N = NormalizeSafe3(-camF, float3(0, 0, 1));

    OUT.PosWS = worldPos;
    OUT.NormalWS = N;
    OUT.UV = IN.UV;
    OUT.Pos = mul(float4(worldPos, 1.0f), g_FrameCB.ViewProj);

    return OUT;
}
