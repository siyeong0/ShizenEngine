#include "HLSL_Structures.hlsli"

cbuffer SHADOW_CONSTANTS
{
    ShadowConstants g_ShadowCB;
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
};

struct PSInput
{
	float4 Pos : SV_POSITION;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    ObjectConstants oc = g_ObjectTable[g_ObjectIndex];
    float4 WPos = mul(float4(VSIn.Pos, 1.0), oc.World);
    PSIn.Pos = mul(WPos, g_ShadowCB.LightViewProj);
}
