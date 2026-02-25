#ifndef HLSL_STRUCTURES_HLSLI
#define HLSL_STRUCTURES_HLSLI

static const uint DEFAULT_LIT = 0;
static const uint FOLIAGE = 1;

// ----------------------------------------------
// Constant buffers
// ----------------------------------------------
struct FrameConstants
{
	uint FrameIndex;
	float3 CameraPosition;

	float4x4 View;
	float4x4 Proj;
	float4x4 ViewProj;
	
	float4x4 InvView;
	float4x4 InvProj;
	float4x4 InvViewProj;

	float4 FrustumPlanesWS[6];

	float2 ViewportSize;
	float2 InvViewportSize;

	float NearPlane;
	float FarPlane;
	float DeltaTime;
	float CurrTime;
	
	float PrevDeltaTime;
	float PrevCurrTime;

	float Exposure;
	float PreExposure;
	
	float LightIntensity;
	float3 LightDirWS;
	
	float3 LightColor;
	uint FunctionFlags;

	float4x4 LightViewProj;
};

struct ShadowConstants
{
	uint CascadeIndex;
};

struct ViewConstants
{
	float4x4 View;
	float4x4 Proj;
	float4x4 ViewProj;
	float4x4 InvViewProj;
	float4x4 PrevViewProj;
};

// ------------------------------------------------------------
// CascadeAttribs
// - NO ARRAYS inside CB -> this struct is embedded as Cascades0..7
// - Frustum-fit requires per-cascade light view (translation differs)
// ------------------------------------------------------------
struct CascadeAttribs
{
	float4x4 WorldToLightView; // per-cascade world -> light view
	float4 LightSpaceScale; // xyz
	float4 LightSpaceScaledBias; // xyz
	float4 StartEndZ; // x=start, y=end (camera view-space +z)
	float4 MarginProjSpace; // xyzw
};

// ------------------------------------------------------------
// ShadowMapAttribs (NO ARRAYS INSIDE CB)
// ------------------------------------------------------------
struct ShadowMapAttribs
{
	float4 ShadowMapDim; // xy = size, zw = inv size

	// replaces float4 CascadeCamSpaceZEnd[2]
	float4 CascadeCamSpaceZEnd0; // for cascades 0..3
	float4 CascadeCamSpaceZEnd1; // for cascades 4..7

	int NumCascades;
	float NumCascadesF;
	float CascadeTransitionRegion;
	float ReceiverPlaneDepthBiasClamp;

	float FixedDepthBias;
	float FilterWorldSize;
	int FixedFilterSize;
	int MaxAnisotropy;

	// replaces CascadeAttribs Cascades[8]
	CascadeAttribs Cascades0;
	CascadeAttribs Cascades1;
	CascadeAttribs Cascades2;
	CascadeAttribs Cascades3;
	CascadeAttribs Cascades4;
	CascadeAttribs Cascades5;
	CascadeAttribs Cascades6;
	CascadeAttribs Cascades7;
};

// ---------------------------------------------------------------------------
// Draw constants (PER DRAW)
// ---------------------------------------------------------------------------
struct DrawConstants
{
	uint StartInstanceLocation;
	uint _pad0;
	uint _pad1;
	uint _pad2;
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
// Terrain
// ----------------------------------------------
struct TerrainConstants
{
	float2 WorldOriginXZ;
	float2 WorldSizeXZ;

	float ChunkSize;
	float InvChunkSize;

	float WorldSpacing;
	float InvWorldSpacing;

	float2 HeightTexelSize;
	float HeightScale;
	float HeightOffset;

	uint CenterXZ;
	float NormalUpBias;
	uint ChunkGridRes;
	float InvChunkGridRes;
};

struct TerrainDrawConstants
{
	float2 ChunkOriginXZ;
	uint LodIndex;
	uint _pad0;

	float2 ChunkSizeXZ;
	float2 InvChunkSizeXZ;
};

// ----------------------------------------------
// Indirect
// ----------------------------------------------
#define MAX_NUM_INDIRECTS 2048
#define MAX_NUM_INDIRECT_MESHES 8192

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
// Grass instances
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
#define MAX_GRASS_SPECIES 256
#endif

struct GrassGenConstants
{
	uint NumSpecies; // total species count (base + special)
	uint NumGrassTypes; // flattened type count (sum variations)
	uint SamplesPerChunk;
	int ChunkHalfExtent;

	uint NumBaseSpecies; // number of base species
	uint NumSpecialSpecies; // number of special species
	float DensityContrast;
	float DensityPow;
	
	uint ChunkVisibleDim; // dim = 2*ChunkHalfExtent
	float ChunkSize;
	float SpawnRadius;
	float Jitter;

	uint SeedSalt;
	float YOffset;
	float NormalAlignStrength;
	float _pad0;

	float LOD0Distance;
	float LOD1Distance;
	float LodHysteresis;
	float _pad1;

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
	
	float ErosionStrength;
	float ErosionNoiseScale;
	float ErosionSmoothness;
	float ErosionMaxDist;
	
	float3 DryTint;
	float DrySaturationReduct;
	float DryDarken;
	float DryRoughness;
};

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
