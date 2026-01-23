#include "HLSL_Structures.hlsli"

cbuffer SHADOW_CONSTANTS
{
    ShadowConstants g_ShadowCB;
};

cbuffer DRAW_CONSTANTS
{
    DrawConstants g_DrawCB;
};

StructuredBuffer<ObjectConstants> g_ObjectTable;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

void main(in VSInput VSIn, out PSInput PSIn, uint instanceID : SV_InstanceID)
{
    ObjectConstants oc = g_ObjectTable[g_DrawCB.StartInstanceLocation + instanceID];

    float4 WPos = mul(float4(VSIn.Pos, 1.0), oc.World);
    PSIn.Pos = mul(WPos, g_ShadowCB.LightViewProj);

    PSIn.UV = VSIn.UV;
}
