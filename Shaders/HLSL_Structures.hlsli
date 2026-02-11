#ifndef HLSL_STRUCTURES_HLSLI
#define HLSL_STRUCTURES_HLSLI

// ----------------------------------------------
// Constant buffers
// ----------------------------------------------
struct FrameConstants
{
    float3 CameraPosition;
	uint FrameIndex;

    float4 FrustumPlanesWS[6];

    float2 ViewportSize;
    float2 InvViewportSize;
    
    float NearPlane;
    float FarPlane;
    float DeltaTime;
    float CurrTime;

    float3 LightDirWS;
    float _pad0;
    
    float3 LightColor;
    float LightIntensity;
    
    float4x4 LightViewProj;
};

struct ViewConstants
{
    float4x4 View;
    float4x4 Proj;
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float4x4 PrevViewProj;
};

struct DrawConstants
{
    uint StartInstanceLocation;
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
	float4x4 PrevWorld;
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
struct GrassMeshInstance
{
	float3 PosWS; // world position (x,y,z)
	float Scale; // uniform scale

    // Angles packed:
    // [ 0..15]  Yaw   UNORM16 (0..2pi)
    // [16..31]  Pitch UNORM16 (mapped from [-MaxPitch..+MaxPitch])
	uint PackedAngles;

    // PackedParams layout:
    // [ 0.. 7]  BendStrength UNORM8
    // [ 8..15]  Press        UNORM8
    // [16..23]  VariantId    (0..255)  // mesh/texture variation index
    // [24..31]  Seed8        (0..255)  // stable random for shading
	uint PackedParams;
};

struct GrassCrossPlaneInstance
{
	float3 PosWS;
	float Scale;

    // Packed0 layout:
    // [ 0..15]  Yaw UNORM16 (0..2pi)
    // [16..23]  VariantId (0..255)   // choose texture/mesh variation
    // [24..31]  Seed8     (0..255)   // stable random
	uint Packed0;

    // Packed1 layout:
    // [ 0.. 7]  BendStrength UNORM8
    // [ 8..15]  Press        UNORM8
    // [16..23]  Reserved / AtlasFrame (0..255) optional
    // [24..31]  Flags        (bitfield) optional
	uint Packed1;
};

struct GrassBillboardInstance
{
	float3 PosWS;
	float Scale;

    // Packed layout:
    // [ 0..15]  Yaw UNORM16 (0..2pi)
    // [16..23]  Impostor/AtlasIndex (0..255)  // choose billboard frame set
    // [24..31]  Seed8 (0..255)
	uint Packed;
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
    
    // Interaction field mapping (world->interaction local uv)
    float2 InteractionOriginXZ;
    float2 InteractionInvWorldSizeXZ; // 1 / FieldWorldSizeXZ
    
    uint2 InteractionTexelOrigin; // ring buffer origin (0..FieldSize-1)
    float2 InteractionInvFieldSize; // 1/FieldSize (e.g. 1/4096)
};

// ----------------------------------------------
// Grass rendering constants (VS/PS)
// ----------------------------------------------
struct GrassRenderConstants
{
    float4 BaseColorFactor;
    float4 Tint;

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

// Global per-frame constants (mapped once per frame)
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

    // Sliding field mapping
    float2 FieldOriginXZ; // world-space origin (meters) of LOCAL field window
    float2 FieldWorldSizeXZ; // world coverage size (meters)

    // Ring mapping: which texel corresponds to FieldOriginXZ (0..W-1)
    uint2 TexelOrigin; // ring offset in texels
    uint _Pad1;
    uint _Pad2;
};

// Per-dispatch constants (updated many times per frame)
struct InteractionDispatch
{
    // Rect in "LOCAL texel space" [0..W), [0..H) (not ring-space)
    // We convert local->ring inside shader using TexelOrigin.
    uint2 RectOffset; // local offset in texels
    uint2 RectSize; // local size in texels

    // For stamp-based apply
    uint StampIndex; // which stamp to apply
    uint Mode; // 0 = ClearRect, 1 = ApplySingleStamp
    uint2 _Pad;
};


#endif // HLSL_STRUCTURES_HLSLI
