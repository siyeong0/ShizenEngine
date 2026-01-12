#include "shader_structs.fxh"

cbuffer ShaderConstants
{
    Constants g_Constants;
}

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;

    float4 LightLocation : ATTRIB2;
    float3 LightColor : ATTRIB3;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float4 LightLocation : LIGHT_LOCATION;
    float3 LightColor : LIGHT_COLOR;
};

void VS_MAIN(in uint InstID : SV_InstanceID, in VSInput VSIn, out PSInput PSIn)
{
    float3 Pos = VSIn.LightLocation.xyz + VSIn.Pos.xyz * VSIn.LightLocation.w;
    PSIn.Pos = mul(float4(Pos, 1.0), g_Constants.ViewProj);

    PSIn.LightLocation = VSIn.LightLocation;
    PSIn.LightColor = VSIn.LightColor;
}



Texture2D<float4> g_SubpassInputColor;
SamplerState g_SubpassInputColor_sampler;

Texture2D<float4> g_SubpassInputDepthZ;
SamplerState g_SubpassInputDepthZ_sampler;

struct PSOutput
{
    float4 Color : SV_TARGET0;
};

void PS_MAIN(in PSInput PSIn, out PSOutput PSOut)
{
    float Depth = g_SubpassInputDepthZ.Load(int3(PSIn.Pos.xy, 0)).x;
    float4 ClipSpacePos = float4(PSIn.Pos.xy * g_Constants.ViewportSize.zw * float2(2.0, -2.0) + float2(-1.0, 1.0), Depth, 1.0);
    float4 WorldPos = mul(ClipSpacePos, g_Constants.ViewProjInv);
    WorldPos.xyz /= WorldPos.w;
    float DistToLight = length(WorldPos.xyz - PSIn.LightLocation.xyz);
    float LightRadius = PSIn.LightLocation.w;
    float Attenuation = clamp(1.0 - DistToLight / LightRadius, 0.0, 1.0);
    if (Attenuation == 0.0 && g_Constants.ShowLightVolumes == 0)
    {
        discard;
    }

    float3 Color = g_SubpassInputColor.Load(int3(PSIn.Pos.xy, 0)).rgb;
    PSOut.Color.rgb = Color.rgb * PSIn.LightColor.rgb * Attenuation;
    if (g_Constants.ShowLightVolumes != 0)
        PSOut.Color.rgb += PSIn.LightColor.rgb * 0.05;

    PSOut.Color.a = 1.0;
}