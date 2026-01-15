#include "HLSL_Structures.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer OBJECT_CONSTANTS
{
    ObjectConstants g_ObjectCB;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
    float3 Normal : ATTRIB2;
    float3 Tangent : ATTRIB3;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float3 WorldN : TEXCOORD2;
    float3 WorldT : TEXCOORD3;
    float Depth01 : TEXCOORD4;
};

void main(in VSInput VSIn, out VSOutput VSOut)
{
    float4 worldPos4 = mul(float4(VSIn.Pos, 1.0), g_ObjectCB.World);
    VSOut.WorldPos = worldPos4.xyz;
    float4 clip = mul(worldPos4, g_FrameCB.ViewProj);
    VSOut.Pos = clip;

    VSOut.UV = VSIn.UV;

    VSOut.Depth01 = clip.z / max(clip.w, 1e-8);

    // Correct normal/tangent transform for non-uniform scale
    // float3 N = normalize(mul(VSIn.Normal, g_ObjectCB.WorldInvTranspose));
    float3 N = normalize(mul(VSIn.Normal, (float3x3) g_ObjectCB.WorldInvTranspose));
    float3 T = normalize(mul(VSIn.Tangent, (float3x3) g_ObjectCB.WorldInvTranspose));

    // Orthonormalize T against N
    T = normalize(T - N * dot(N, T));

    VSOut.WorldN = N;
    VSOut.WorldT = T;
}
