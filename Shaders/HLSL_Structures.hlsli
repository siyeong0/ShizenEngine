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
    float4x4 InvViewProj;
    
    float3 CameraPosition;
    float _pad0;

    float2 ViewportSize;
    float2 InvViewportSize;

    float NearPlane;
    float FarPlane;
    float DeltaTime;
    float CurrTime;
    
    float4x4 LightViewProj;
    float3 LightDirWS;
    float _pad1;
    float3 LightColor;
    float LightIntensity;
};

struct DrawConstants
{
    uint StartInstanceLocation;
};

struct ShadowConstants
{
    float4x4 LightViewProj;
};

static const uint MAT_HAS_BASECOLOR = 1u << 0;
static const uint MAT_HAS_NORMAL = 1u << 1;
static const uint MAT_HAS_MR = 1u << 2;
static const uint MAT_HAS_AO = 1u << 3;
static const uint MAT_HAS_EMISSIVE = 1u << 4;
static const uint MAT_HAS_HEIGHT = 1u << 5;


struct ObjectConstants
{
    float4x4 World; // Local ¡æ World
    float4x4 WorldInvTranspose; // Normal matrix
};

struct GrassInstance
{
    float3 PosWS;
    float Yaw; // rad

    float Scale; // uniform scale
    float _pad1;
    float _pad2;
    uint Flags;
};

struct GrassConstants
{
    float4 BaseColorFactor;
    float4 Tint;
    
    float AlphaCut;
    float Ambient;
    uint MaterialFlags;
    uint _pad1;
};

// Must exist for C++ side too (Renderer.cpp includes this file under namespace hlsl)
struct ObjectIndexConstants
{
    uint ObjectIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};


#endif // HLSL_STRUCTURES_HLSLI
