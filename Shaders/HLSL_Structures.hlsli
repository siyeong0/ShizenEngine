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

    float4 FrustumPlanesWS[6];
    
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

// ----------------------------------------------
// Material flags
// ----------------------------------------------
static const uint MAT_HAS_BASECOLOR = 1u << 0;
static const uint MAT_HAS_NORMAL = 1u << 1;
static const uint MAT_HAS_MR = 1u << 2;
static const uint MAT_HAS_AO = 1u << 3;
static const uint MAT_HAS_EMISSIVE = 1u << 4;
static const uint MAT_HAS_HEIGHT = 1u << 5;

// ----------------------------------------------
// Object
// ----------------------------------------------
struct ObjectConstants
{
    float4x4 World;
    float4x4 WorldInvTranspose;
};

// ----------------------------------------------
// Grass instance (GPU generated)
// ----------------------------------------------
struct GrassInstance
{
    float3 PosWS;
    float Scale; // uniform scale
    
    float Yaw;
    float Pitch;
    float BendStrength; // base bend amount (0..1-ish)
    uint _pad0;
};

// ----------------------------------------------
// Grass generation constants (Compute)
// ----------------------------------------------
struct GrassGenConstants
{
    // --- Terrain / Height decode ---
    float HeightScale;
    float HeightOffset;
    float YOffset;
    float _padT0;

    uint HFWidth;
    uint HFHeight;
    uint CenterXZ; // 0/1
    uint _padT1;

    float SpacingX;
    float SpacingZ;
    float _padT2;
    float _padT3;

    // --- Chunk placement ---
    float ChunkSize; // meters
    int ChunkHalfExtent; // half grid around camera
    uint SamplesPerChunk;
    float Jitter; // 0..1

    float MinScale;
    float MaxScale;
    float SpawnProb; // keep if rand <= SpawnProb
    float SpawnRadius; // meters

    float BendStrengthMin; // base bend random range
    float BendStrengthMax;
    uint SeedSalt; // salt for deterministic placement
    uint _padT4;
};

// ----------------------------------------------
// Grass rendering constants (VS/PS)
// ----------------------------------------------
struct GrassRenderConstants
{
    float4 BaseColorFactor;
    float4 Tint;

    float AlphaCut;
    float Ambient;
    float ShadowStregth;
    float DirectLightStrength;

    // Wind (world-space)
    float2 WindDirXZ; // should be normalized
    float WindStrength; // radians scale (or angle scale)
    float WindSpeed; // time scale

    float WindFreq; // spatial frequency
    float WindGust; // extra gust amplitude
    float MaxBendAngle; // radians, clamp total bend
    float _pad1;
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
