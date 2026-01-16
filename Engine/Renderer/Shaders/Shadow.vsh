#include "HLSL_Structures.hlsli"

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
};

struct PSInput
{
	float4 Pos : SV_POSITION;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    float4 WPos = mul(float4(VSIn.Pos, 1.0), g_ObjectCB.World);
    PSIn.Pos = mul(WPos, g_ShadowCB.LightViewProj);
}
