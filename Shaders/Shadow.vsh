#include "HLSL_Structures.hlsli"

cbuffer SHADOW_CONSTANTS
{
    ShadowConstants g_ShadowCB;
};

StructuredBuffer<ObjectConstants> g_ObjectTable;

struct VSInput
{
    float3 Pos : ATTRIB0;

    // Per-instance data (bound via instance vertex buffer, PER_INSTANCE)
    uint ObjectIndex : ATTRIB4;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    ObjectConstants oc = g_ObjectTable[VSIn.ObjectIndex];

    float4 WPos = mul(float4(VSIn.Pos, 1.0), oc.World);
    PSIn.Pos = mul(WPos, g_ShadowCB.LightViewProj);
}
