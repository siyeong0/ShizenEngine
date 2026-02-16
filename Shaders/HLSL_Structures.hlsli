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

// ---------------------------------------------------------------------------
// Draw constants (PER DRAW)
// - Grass uses SpeciesId + LodIndex to look up base offsets in a buffer.
// - StartInstanceLocation kept for compatibility; you can ignore it for grass.
// ---------------------------------------------------------------------------
struct DrawConstants
{
	uint StartInstanceLocation; // optional legacy
	uint SpeciesId; // used by grass
	uint LodIndex; // 0/1/2 for grass
	uint _pad0;
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
struct TerrainConstants
{
	float2 WorldOriginXZ;
	float2 WorldSizeXZ;
	float2 ChunkSizeXZ;
	float2 WorldSpacingXZ;

	float2 HeightTexelSize;
	float HeightScale;
	float HeightOffset;

	uint CenterXZ;
	float NormalUpBias;
	uint ChunkGridRes;
	float InvChunkGridRes; 
};

// ----------------------------------------------
// Terrain
// ----------------------------------------------
struct TerrainDrawConstants
{
	float2 ChunkOriginXZ;
	uint LodIndex;
	float _pad0;
};

// ----------------------------------------------
// Indirect
// ----------------------------------------------
#define MAX_NUM_INDIRECTS 1024
#define MAX_NUM_INDIRECT_MESHES 2048

struct IndirectArgsTemplate
{
	uint IndexCountPerInstance;
	uint StartIndexLocation;
	uint BaseVertexLocation;
	uint StartInstanceLocation;
};

struct IndirectArgsHeader
{
	uint NumSlots;
	uint NumMeshes;
	uint MaxInstances;
	uint _pad0;
};

// ----------------------------------------------
// Grass instance
// ----------------------------------------------
struct GrassMeshInstance
{
	float3 PosWS;
	float Scale;
	uint PackedAngles;
	uint PackedParams;
};

struct GrassCrossPlaneInstance
{
	float3 PosWS;
	float Scale;
	uint Packed0;
	uint Packed1;
};

struct GrassBillboardInstance
{
	float3 PosWS;
	float Scale;
	uint Packed;
};

// ----------------------------------------------
// Grass generation constants (Compute)
// ----------------------------------------------
#ifndef MAX_GRASS_SPECIES
#define MAX_GRASS_SPECIES 16
#endif

struct GrassGenConstants
{
	// Species count
	uint NumSpecies;
	float YOffset;
	float NormalAlignStrength;
	float _pad0;

	float LOD0Distance;
	float LOD1Distance;
	float LodHysteresis;
	float _pad1;

	uint ChunkVisibleDim;
	float ChunkSize;

	int ChunkHalfExtent;
	uint SamplesPerChunk;
	uint NumPools;
	float Jitter;

	float MinScale;
	float MaxScale;
	float SpawnProb;
	float SpawnRadius;

	float BendStrengthMin;
	float BendStrengthMax;
	uint SeedSalt;
	uint _pad2;

	float DensityContrast;
	float DensityPow;
	float _pad3;
	float _pad4;

	float SlopeToDensity;
	float HeightMinN;
	float HeightMaxN;
	float HeightFadeN;

	float2 InteractionOriginXZ;
	float2 InteractionInvWorldSizeXZ;

	uint2 InteractionTexelOrigin;
	float2 InteractionInvFieldSize;
};

// ----------------------------------------------
// Grass rendering constants
// ----------------------------------------------
struct GrassRenderConstants
{
	float4 BaseColorFactor;
	float4 Tint;

	float2 WindDirXZ;
	float WindStrength;
	float WindSpeed;

	float WindFreq;
	float WindGust;
	float MaxBendAngle;
	float _pad1;

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

	float2 FieldOriginXZ;
	float2 FieldWorldSizeXZ;

	uint2 TexelOrigin;
	uint _Pad1;
	uint _Pad2;
};

struct InteractionDispatch
{
	uint2 RectOffset;
	uint2 RectSize;

	uint StampIndex;
	uint Mode;
	uint2 _Pad;
};

#endif // HLSL_STRUCTURES_HLSLI
