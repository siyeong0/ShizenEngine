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
    float Press; // Interaction (0..1): sampled from g_InteractionField in GenCS
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
    float SpawnProb; // base probability
    float SpawnRadius; // meters

    float BendStrengthMin;
    float BendStrengthMax;
    uint SeedSalt;
    uint _padT4;

    // Density field (world tiled) tuning
    float DensityTiling; // meters -> uv. ex) 0.02 => 50m period (1/50)
    float DensityContrast; // 0..0.49. ex) 0.25 => smoothstep(0.25, 0.75)
    float DensityPow; // < 1 boosts mids. ex) 0.65
    float _padD0;

    // Slope/Height masks
    float SlopeToDensity; // slope (tan-ish) -> 0..1 scale. ex) 0.12~0.25
    float HeightMinN; // normalized 0..1 (heightmap space)
    float HeightMaxN; // normalized 0..1
    float HeightFadeN; // normalized fade width. ex) 0.02~0.05
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
    
    // Interaction bending (Zelda-ish)
    float InteractionBendAngle; // radians, e.g. 1.1 (~63 deg)
    float InteractionSink; // meters, e.g. 0.05
    float InteractionWindFade; // 0..1, pressed -> reduce wind (e.g. 0.7)
};

// Must exist for C++ side too (Renderer.cpp includes this file under namespace hlsl)
struct ObjectIndexConstants
{
    uint ObjectIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};


// Interaction
static const uint INTERACTION_STAMP_NONE = 0;
static const uint INTERACTION_STAMP_SUBTRACT = 1u << 0;
static const uint INTERACTION_STAMP_MAX_BLEND = 1u << 1;

struct InteractionStamp
{
    float2 CenterXZ;
    float Radius;
    float Strength;

    uint TargetId;
    uint Flags;
    float FalloffPower;
    float _Pad0;
};

struct InteractionConstants
{
    uint FieldWidth;
    uint FieldHeight;
    uint NumStamps;
    float DeltaTime;

    float DecayPerSec; // e.g. 1.5 (press value decreases by 1.5 per second)
    float ClampMax; // e.g. 1.0
    float ClampMin; // e.g. 0.0
    float _Pad0;
};

#endif // HLSL_STRUCTURES_HLSLI
