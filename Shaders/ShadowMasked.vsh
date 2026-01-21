#include "HLSL_Structures.hlsli"

cbuffer SHADOW_CONSTANTS
{
    ShadowConstants g_ShadowCB;
};

StructuredBuffer<ObjectConstants> g_ObjectTable;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;

    // Per-instance data (PER_INSTANCE)
    uint ObjectIndex : ATTRIB4;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    ObjectConstants oc = g_ObjectTable[VSIn.ObjectIndex];

    float4 WPos = mul(float4(VSIn.Pos, 1.0), oc.World);
    PSIn.Pos = mul(WPos, g_ShadowCB.LightViewProj);

    PSIn.UV = VSIn.UV;
}
