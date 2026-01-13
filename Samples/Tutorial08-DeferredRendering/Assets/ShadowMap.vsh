#include "HLSL_Common.hlsli"

cbuffer SHADOW_CONSTANTS
{
    ShadowConstants g_ShadowCB;
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
};

void main(in VSInput VSIn, out VSOutput VSOut)
{
    float4x4 MVP = mul(g_ObjectCB.World, g_ShadowCB.LightViewProj);
    VSOut.Pos = mul(float4(VSIn.Pos, 1.0), MVP);
}
