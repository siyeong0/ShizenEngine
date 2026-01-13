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
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldN : TEXCOORD1;
    float Depth01 : TEXCOORD2;
};

void main(in VSInput VSIn, out VSOutput VSOut)
{
    float4x4 MVP = mul(g_ObjectCB.World, g_FrameCB.ViewProj);
    float4 clip = mul(float4(VSIn.Pos, 1.0), MVP);
    VSOut.Pos = clip;

    VSOut.UV = VSIn.UV;

    float ndcZ = clip.z / max(clip.w, 1e-8);
    VSOut.Depth01 = ndcZ;

    VSOut.WorldN = normalize(mul(VSIn.Normal, transpose((float3x3)g_ObjectCB.World)));
}
