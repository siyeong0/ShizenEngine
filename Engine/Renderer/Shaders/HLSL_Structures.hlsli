#ifndef HLSL_STRUCTURES_HLSLI
#define HLSL_STRUCTURES_HLSLI

// ----------------------------------------------
// Constant buffers
// ----------------------------------------------
struct FrameConstants
{
    float4x4 View;
    float4x4 Proj;
    float4x4 ViewProj;

    float3 CameraPosition;
    float _pad0;

    float2 ViewportSize;
    float2 InvViewportSize;

    float NearPlane;
    float FarPlane;
    float DeltaTime;
    float CurrTime;
};

struct MaterialConstants
{
    float4 BaseColorFactor; // rgb = base color, a = opacity
    float3 EmissiveFactor; // emissive rgb
    float MetallicFactor; // scalar

    float RoughnessFactor; // scalar
    float NormalScale; // scalar
    float OcclusionStrength; // scalar
    float AlphaCutoff; // scalar (for MASK)

    uint Flags; // bitmask HAS_*
    uint3 _pad0; // 16-byte align
};

static const uint MAT_HAS_BASECOLOR = 1u << 0;
static const uint MAT_HAS_NORMAL = 1u << 1;
static const uint MAT_HAS_MR = 1u << 2;
static const uint MAT_HAS_AO = 1u << 3;
static const uint MAT_HAS_EMISSIVE = 1u << 4;


struct ObjectConstants
{
    float4x4 World; // Local ¡æ World
    float4x4 WorldInvTranspose; // Normal matrix
};

#endif // HLSL_STRUCTURES_HLSLI
