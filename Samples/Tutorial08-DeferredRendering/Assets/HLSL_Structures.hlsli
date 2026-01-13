#ifndef TUTORIAL08_STRUCTURES_HLSLI
#define TUTORIAL08_STRUCTURES_HLSLI

// ----------------------------------------------
// Constant buffers
// ----------------------------------------------
struct ShaderConstants
{
    float4x4 ViewProj;
    float4x4 ViewProjInv;

    // x,y = viewport size in pixels
    // z,w = 1/viewport size
    float4 ViewportSize;

    float3 CameraPosWS;
    int LightsCount; 

    int ShowLightVolumes;
    int Padding0;
    int Padding1;
};

struct ShadowConstants
{
    float4x4 LightViewProj;

    float2 ShadowMapTexelSize; // (1/width, 1/height)
    float ShadowBias; // typical: 0.0005~0.005 (tune per scene)
    float ShadowStrength; // 0..1 (1 = full shadow)

    float3 LightDirWS;
    float PaddingS0;
};

struct ObjectConstants
{
    float4x4 World;
    float3x3 WorldInvertTranspose;

    float PaddingO0;
    float PaddingO1;
    float PaddingO2;
};

// ----------------------------------------------
// Scene data
// ----------------------------------------------
struct LightAttribs
{
    float3 Location;
    float Radius;
    float3 Color;
    float Padding;
};

#endif // TUTORIAL08_STRUCTURES_HLSLI
