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
// HeightField (COMMON for Terrain/Grass/etc.)
// ----------------------------------------------
// Convention:
// - Height texture stores normalized height in [0..1] sampled via linear clamp.
// - WorldSizeXZ = ((Width-1)*SpacingX, (Height-1)*SpacingZ)
// - WorldOriginXZ = (centered ? -0.5*WorldSize : 0) for each axis
// - HeightTexelSize = (1/Width, 1/Height)
struct HeightFieldConstants
{
    float2 WorldOriginXZ;
    float2 WorldSizeXZ;

    float2 WorldSpacingXZ; // (SpacingX, SpacingZ) in meters
    float2 HeightTexelSize; // (1/Width, 1/Height)

    float HeightScale; // meters
    float HeightOffset; // meters
    uint CenterXZ; // 0/1 (optional, mostly for debugging)
    float NormalUpBias;
};

// ----------------------------------------------
// Terrain
// ----------------------------------------------

struct TerrainDrawConstants
{
    // Chunk placement in world
    float2 ChunkOriginXZ;
    float2 ChunkSizeXZ;

    // Height UV mapping (optional extra scale/bias on top of base World->UV)
    float2 HeightUVScale;
    float2 HeightUVBias;

    // Surface UV mapping for material/tiling
    float2 SurfaceUVScale;
    float2 SurfaceUVBias;

    // Normal sampling step multiplier (>= 1)
    float NormalSampleStep;
    
    float LodMorphAlpha;
    uint LodIndex;
    float _pad0;
    
    float4 DebugChunkColor;
};

// ----------------------------------------------
// Indirect
// ----------------------------------------------
#define MAX_NUM_INDIRECTS 256

struct IndirectArgsTemplate
{
    uint IndexCountPerInstance;
    uint StartIndexLocation;
    uint BaseVertexLocation;
    uint StartInstanceLocation;
};

struct IndirectConstants
{
    uint NumSlots;
    uint MaxInstances;
    uint Pad0;
    uint Pad1;

    IndirectArgsTemplate Templates[MAX_NUM_INDIRECTS];
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
    float Press; // Interaction (0..1)
};

// ----------------------------------------------
// Grass generation constants (Compute)
// ----------------------------------------------
// NOTE: Height decode is shared via HeightFieldConstants HF.
struct GrassGenConstants
{
    uint IndirectSlotLOD0;
    uint IndirectSlotLOD1;
    uint IndirectSlotLOD2;
    uint _pad0;
    
    float LOD0Distance;
    float LOD1Distance;
    float LodHysteresis;
    float _pad1;
    
    // Optional extra vertical offset for grass placement (meters)
    float YOffset;
    float MinPitch;
    float MaxPitch;
    float _pad2;

    // Chunk placement
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
    uint _pad3;

    // Density field (world tiled) tuning
    float DensityTiling; // meters -> uv
    float DensityContrast; // 0..0.49
    float DensityPow; // curve
    float _pad4;

    // Slope/Height masks
    float SlopeToDensity; // slope -> 0..1
    float HeightMinN; // normalized 0..1
    float HeightMaxN; // normalized 0..1
    float HeightFadeN; // normalized fade width
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
    float2 WindDirXZ;
    float WindStrength;
    float WindSpeed;

    float WindFreq;
    float WindGust;
    float MaxBendAngle;
    float _pad1;

    // Interaction bending
    float InteractionBendAngle;
    float InteractionSink;
    float InteractionWindFade;
    float _pad2;
};

// Must exist for C++ side too
struct ObjectIndexConstants
{
    uint ObjectIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

// ----------------------------------------------
// Interaction
// ----------------------------------------------
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

    float DecayPerSec;
    float ClampMax;
    float ClampMin;
    float _Pad0;
};

#endif // HLSL_STRUCTURES_HLSLI
