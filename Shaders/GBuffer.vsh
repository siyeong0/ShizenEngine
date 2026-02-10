#include "Common.hlsli"

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
	float4 SVPosition : SV_POSITION;
	float4 CurrClip : TEXCOORD0;
	float4 PrevClip : TEXCOORD1;
	float2 UV : TEXCOORD2;
	float3 WorldPos : TEXCOORD3;
	float3 WorldN : TEXCOORD4;
	float3 WorldT : TEXCOORD5;
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
    OUT.SVPosition = ApplyTAAJittering(mul(worldPos4, g_FrameCB.ViewProj));
	OUT.CurrClip = mul(worldPos4, g_FrameCB.ViewProj);
    
	float4 prevWorldPos4 = mul(float4(IN.Pos, 1.0), oc.PrevWorld);
	OUT.PrevClip = mul(prevWorldPos4, g_FrameCB.PrevViewProj);
    
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
