#include "HLSL_Structures.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer OBJECT_INDEX
{
    uint g_ObjectIndex;
    uint3 _pad_ObjectIndex;
};

StructuredBuffer<ObjectConstants> g_ObjectTable;

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
};

void main(in VSInput VSIn, out VSOutput VSOut)
{
    ObjectConstants oc = g_ObjectTable[g_ObjectIndex];

    float4 worldPos4 = mul(float4(VSIn.Pos, 1.0), oc.World);
    VSOut.WorldPos = worldPos4.xyz;

    float4 clip = mul(worldPos4, g_FrameCB.ViewProj);
    VSOut.Pos = clip;

    VSOut.UV = VSIn.UV;

    // Correct normal/tangent transform for non-uniform scale
    float3 N = normalize(mul(VSIn.Normal, (float3x3) oc.WorldInvTranspose));
    float3 T = normalize(mul(VSIn.Tangent, (float3x3) oc.WorldInvTranspose));

    // Orthonormalize T against N
    T = normalize(T - N * dot(N, T));

    VSOut.WorldN = N;
    VSOut.WorldT = T;
}
