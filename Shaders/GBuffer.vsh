#include "HLSL_Structures.hlsli"

// Constant Buffers
cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
};

cbuffer DRAW_CONSTANTS
{
    DrawConstants g_DrawCB;
};

// Resources
StructuredBuffer<ObjectConstants> g_ObjectTable;

// Input / Output
struct VSInput
{
    float3 Pos     : ATTRIB0;
    float2 UV      : ATTRIB1;
    float3 Normal  : ATTRIB2;
    float3 Tangent : ATTRIB3;
};

struct VSOutput
{
    float4 Pos      : SV_POSITION;
    float2 UV       : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float3 WorldN   : TEXCOORD2;
    float3 WorldT   : TEXCOORD3;
};

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
void main(in VSInput IN, out VSOutput OUT, uint instanceID : SV_InstanceID)
{
    ObjectConstants oc = g_ObjectTable[g_DrawCB.StartInstanceLocation + instanceID];

    // World position
    float4 worldPos4 = mul(float4(IN.Pos, 1.0), oc.World);
    OUT.WorldPos = worldPos4.xyz;

    // Clip position
    OUT.Pos = mul(worldPos4, g_FrameCB.ViewProj);

    // Texcoord
    OUT.UV = IN.UV;

    // World-space normal/tangent
    float3 N = normalize(mul(IN.Normal,  (float3x3)oc.WorldInvTranspose));
    float3 T = normalize(mul(IN.Tangent, (float3x3)oc.WorldInvTranspose));

    // Orthonormalize tangent against normal
    T = normalize(T - N * dot(N, T));

    OUT.WorldN = N;
    OUT.WorldT = T;
}
