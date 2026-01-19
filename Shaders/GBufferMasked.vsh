#include "HLSL_Structures.hlsli"

cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

StructuredBuffer<ObjectConstants> g_ObjectTable;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
    float3 Normal : ATTRIB2;
    float3 Tangent : ATTRIB3;

    uint ObjectIndex : ATTRIB4; // from instance buffer (PER_INSTANCE)
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
    ObjectConstants oc = g_ObjectTable[VSIn.ObjectIndex];

    float4 worldPos4 = mul(float4(VSIn.Pos, 1.0), oc.World);
    VSOut.WorldPos = worldPos4.xyz;

    VSOut.Pos = mul(worldPos4, g_FrameCB.ViewProj);
    VSOut.UV = VSIn.UV;

    float3 N = normalize(mul(VSIn.Normal, (float3x3) oc.WorldInvTranspose));
    float3 T = normalize(mul(VSIn.Tangent, (float3x3) oc.WorldInvTranspose));
    T = normalize(T - N * dot(N, T));

    VSOut.WorldN = N;
    VSOut.WorldT = T;
}
