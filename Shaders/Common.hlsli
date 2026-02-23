#ifndef HLSL_COMMON_HLSLI
#define HLSL_COMMON_HLSLI

#include "HLSL_Structures.hlsli"

// ============================================================================
// Input
// ============================================================================
struct BaseVSInput
{
	float3 Position : ATTRIB0;
	float2 UV : ATTRIB1;
	float3 Normal : ATTRIB2;
	float3 Tangent : ATTRIB3;
};

#define GET_VERTEX_POS()        (IN.Position)
#define GET_VERTEX_UV()         (IN.UV)
#define GET_VERTEX_NORMAL()     (IN.Normal)
#define GET_VERTEX_TANGENT()    (IN.Tangent)

// ---------------------------------------------------------------------------
// Jitter toggle
// ---------------------------------------------------------------------------
// #define APPLY_JITTER 1

#ifdef APPLY_JITTER
#define APPLY_JITTER_TO_CLIP(clipPos)  ApplyTAAJittering((clipPos))
#else
#define APPLY_JITTER_TO_CLIP(clipPos)  (clipPos)
#endif

// ============================================================================
// GBuffer / regular path (not depth-only)
// ============================================================================
#ifndef DEPTH_ONLY

struct BaseVSOutput
{
	float4 SVPosition : SV_POSITION;
	float4 CurrClip : TEXCOORD0;
	float4 PrevClip : TEXCOORD1;
	float2 UV : TEXCOORD2;
	float3 WorldPosition : TEXCOORD3;
	float3 WorldNormal : TEXCOORD4;
	float3 WorldTangent : TEXCOORD5;
};

#define SET_VSOUT_WORLD_POS_STATIC(pos)                                         \
	OUT.WorldPosition = (pos).xyz;                                              \
	OUT.CurrClip      = mul((pos), g_ViewCB.ViewProj);                          \
	OUT.PrevClip      = mul((pos), g_ViewCB.PrevViewProj);                      \
	OUT.SVPosition    = APPLY_JITTER_TO_CLIP(OUT.CurrClip);

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)                                 \
	OUT.WorldPosition = (curr).xyz;                                             \
	OUT.CurrClip      = mul((curr), g_ViewCB.ViewProj);                         \
	OUT.PrevClip      = mul((prev), g_ViewCB.PrevViewProj);                     \
	OUT.SVPosition    = APPLY_JITTER_TO_CLIP(OUT.CurrClip);

#define SET_VSOUT_UV(uv)            OUT.UV = (uv);
#define SET_VSOUT_WORLD_NORMAL(wn)  OUT.WorldNormal = (wn);
#define SET_VSOUT_WORLD_TANGENT(wt) OUT.WorldTangent = (wt);

#define SET_VSOUT_POS(svPos)        OUT.SVPosition = (svPos);
#define SET_VSOUT_CURR_CLIP(cc)     OUT.CurrClip   = (cc);
#define SET_VSOUT_PREV_CLIP(pc)     OUT.PrevClip   = (pc);

struct BasePSInput
{
	float4 SVPosition : SV_POSITION;
	float4 CurrClip : TEXCOORD0;
	float4 PrevClip : TEXCOORD1;
	float2 UV : TEXCOORD2;
	float3 WorldPosition : TEXCOORD3;
	float3 WorldNormal : TEXCOORD4;
	float3 WorldTangent : TEXCOORD5;

	bool bFrontFace : SV_IsFrontFace;
};

#define GET_PSIN_POS()              (IN.SVPosition)
#define GET_PSIN_CURR_CLIP()        (IN.CurrClip)
#define GET_PSIN_PREV_CLIP()        (IN.PrevClip)
#define GET_PSIN_UV()               (IN.UV)
#define GET_PSIN_WORLD_POS()        (IN.WorldPosition)
#define GET_PSIN_WORLD_NORMAL()     (IN.bFrontFace ? IN.WorldNormal : -IN.WorldNormal)
#define GET_PSIN_WORLD_TANGENT()    (IN.WorldTangent)
#define GET_PSIN_FRONTFACE()        (IN.bFrontFace)

struct BasePSOutput
{
	float4 GBuffer0 : SV_TARGET0; // Albedo.rgb, Opacity.a
	float4 GBuffer1 : SV_TARGET1; // Normal.xyz packed (0..1), unused or normal strength in w
	float4 GBuffer2 : SV_TARGET2; // Metallic, Roughness, AO, AlphaCoverage
	float4 GBuffer3 : SV_TARGET3; // Emissive.rgb, unused in a
	float2 Velocity : SV_TARGET4; // RG = motion vector (uv delta)
};

#define SET_PSOUT_ALBEDO(color)            OUT.GBuffer0.rgb = (color);
#define SET_PSOUT_OPACITY(opacity)         OUT.GBuffer0.a   = (opacity);
#define SET_PSOUT_NORMAL_PACKED(normal)    OUT.GBuffer1     = float4((normal), 1.0);
#define SET_PSOUT_NORMAL(normal)           OUT.GBuffer1     = float4(PackNormal01((normal)), 1.0);
#define SET_PSOUT_METALLIC(metallic)       OUT.GBuffer2.r   = (metallic);
#define SET_PSOUT_ROUGHNESS(roughness)     OUT.GBuffer2.g   = (roughness);
#define SET_PSOUT_AO(ao)                   OUT.GBuffer2.b   = (ao);
#define SET_PSOUT_ALPHACOVERAGE(ac)        OUT.GBuffer2.a   = (ac);
#define SET_PSOUT_EMISSIVE(emissive)       OUT.GBuffer3.rgb = (emissive);
#define SET_PSOUT_SHADING_MODEL(model)	   OUT.GBuffer3.a   = (EncodeShadingModel_U8(model));
#define SET_PSOUT_VELOCITY(velocity)       OUT.Velocity     = (velocity);

#define BASE_VS_MAIN_ENTRY(INST_ID_VAR)    void main(BaseVSInput IN, out BaseVSOutput OUT, uint INST_ID_VAR : SV_InstanceID)
#define BASE_PS_MAIN_ENTRY()              void main(in BasePSInput IN, out BasePSOutput OUT)

// ============================================================================
// Depth-only path
// ============================================================================
#else // DEPTH_ONLY

struct BaseVSOutput
{
	float4 SVPosition : SV_POSITION;

#ifdef MASKED
	float2 UV : TEXCOORD0;
#endif
};

// In depth-only we do not output these.
#define SET_VSOUT_CURR_CLIP(cc)     /* no-op */
#define SET_VSOUT_PREV_CLIP(pc)     /* no-op */
#define SET_VSOUT_WORLD_NORMAL(wn)  /* no-op */
#define SET_VSOUT_WORLD_TANGENT(wt) /* no-op */
#define SET_VSOUT_POS(svPos)        OUT.SVPosition = (svPos);

#ifdef MASKED

	// ------------------------------------------------------------------------
	// DEPTH_ONLY + MASKED
	// - Shadow pass: use g_ShadowAttribs mapping
	// - Non-shadow depth pass: use g_ViewCB.ViewProj (original)
	// ------------------------------------------------------------------------
#ifdef SHADOW

#define SET_VSOUT_WORLD_POS_STATIC(pos)                                \
			OUT.SVPosition = BuildShadowClipFromWorldPos((pos).xyz);

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)                        \
			OUT.SVPosition = BuildShadowClipFromWorldPos((curr).xyz);

		// IMPORTANT: Shadow depth should NOT use TAA jitter.
		// We intentionally bypass APPLY_JITTER_TO_CLIP here.

#else // !SHADOW

#define SET_VSOUT_WORLD_POS_STATIC(pos)                                \
			OUT.SVPosition = APPLY_JITTER_TO_CLIP(mul((pos), g_ViewCB.ViewProj));

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)                        \
			OUT.SVPosition = APPLY_JITTER_TO_CLIP(mul((curr), g_ViewCB.ViewProj));

#endif // SHADOW

#define SET_VSOUT_UV(uv)    OUT.UV = (uv);

#else // OPAQUE (not masked)

	// ------------------------------------------------------------------------
	// DEPTH_ONLY + OPAQUE
	// ------------------------------------------------------------------------
#ifdef SHADOW

#define SET_VSOUT_WORLD_POS_STATIC(pos)                                \
			OUT.SVPosition = BuildShadowClipFromWorldPos((pos).xyz);

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)                        \
			OUT.SVPosition = BuildShadowClipFromWorldPos((curr).xyz);

		// No jitter in shadow depth

#else // !SHADOW

#define SET_VSOUT_WORLD_POS_STATIC(pos)                                \
			OUT.SVPosition = APPLY_JITTER_TO_CLIP(mul((pos), g_ViewCB.ViewProj));

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)                        \
			OUT.SVPosition = APPLY_JITTER_TO_CLIP(mul((curr), g_ViewCB.ViewProj));

#endif // SHADOW

#define SET_VSOUT_UV(uv)    /* no-op */

#endif // MASKED

struct BasePSInput
{
	float4 SVPosition : SV_POSITION;

#ifdef MASKED
	float2 UV : TEXCOORD0;
#endif
};

#define GET_PSIN_POS()          (IN.SVPosition)

#ifdef MASKED
#define GET_PSIN_UV()       (IN.UV)
#else
#define GET_PSIN_UV()       (float2(0.0, 0.0))
#endif

#define GET_PSIN_WORLD_POS()        (float3(0.0, 0.0, 0.0))
#define GET_PSIN_CURR_CLIP()        (float4(0.0, 0.0, 0.0, 1.0))
#define GET_PSIN_PREV_CLIP()        (float4(0.0, 0.0, 0.0, 1.0))
#define GET_PSIN_WORLD_NORMAL()     (float3(0.0, 0.0, 1.0))
#define GET_PSIN_WORLD_TANGENT()    (float3(1.0, 0.0, 0.0))
#define GET_PSIN_FRONTFACE()        (false)

struct BasePSOutput { };

#define SET_PSOUT_ALBEDO(color)            /* no-op */
#define SET_PSOUT_OPACITY(opacity)         /* no-op */
#define SET_PSOUT_NORMAL_PACKED(normal)    /* no-op */
#define SET_PSOUT_NORMAL(normal)           /* no-op */
#define SET_PSOUT_METALLIC(metallic)       /* no-op */
#define SET_PSOUT_ROUGHNESS(roughness)     /* no-op */
#define SET_PSOUT_AO(ao)                   /* no-op */
#define SET_PSOUT_ALPHACOVERAGE(ac)        /* no-op */
#define SET_PSOUT_EMISSIVE(emissive)       /* no-op */
#define SET_PSOUT_SHADING_MODEL(model)	   /* no-op */
#define SET_PSOUT_VELOCITY(velocity)       /* no-op */

#define BASE_VS_MAIN_ENTRY(INST_ID_VAR)    void main(BaseVSInput IN, out BaseVSOutput OUT, uint INST_ID_VAR : SV_InstanceID)
#define BASE_PS_MAIN_ENTRY()               void main(in BasePSInput IN)

#endif // DEPTH_ONLY


// ============================================================================
// Common bindings
// ============================================================================

// Objects
StructuredBuffer<ObjectConstants> g_ObjectTable;

// Constant Buffers
cbuffer FRAME_CONSTANTS
{
	FrameConstants g_FrameCB;
};

cbuffer SHADOW_CONSTANTS
{
	ShadowConstants g_ShadowCB;
};

cbuffer SHADOW_MAP_ATTRIBS
{
	ShadowMapAttribs g_ShadowAttribs;
};

cbuffer VIEW_CONSTANTS
{
	ViewConstants g_ViewCB;
};

cbuffer DRAW_CONSTANTS
{
	DrawConstants g_DrawCB;
};

cbuffer TERRAIN_CONSTANTS
{
	TerrainConstants g_TerrainCB;
};

// Resources
Texture2DArray<float> g_ShadowMapArray;
SamplerComparisonState g_ShadowCmpSampler;

Texture2D g_PerlinNoiseTex;
Texture2D g_BlueNoiseTex;

// Samplers
SamplerState g_LinearWrapSampler;
SamplerState g_LinearClampSampler;
SamplerState g_PointWrapSampler;
SamplerState g_PointClampSampler;

// -----------------------------------------------------------------------------
// Helpers: Cascade accessors (NO ARRAYS IN CB)
// -----------------------------------------------------------------------------
static CascadeAttribs GetCascadeAttribs(uint idx)
{
	switch (idx)
	{
		default:
		case 0:
			return g_ShadowAttribs.Cascades0;
		case 1:
			return g_ShadowAttribs.Cascades1;
		case 2:
			return g_ShadowAttribs.Cascades2;
		case 3:
			return g_ShadowAttribs.Cascades3;
		case 4:
			return g_ShadowAttribs.Cascades4;
		case 5:
			return g_ShadowAttribs.Cascades5;
		case 6:
			return g_ShadowAttribs.Cascades6;
		case 7:
			return g_ShadowAttribs.Cascades7;
	}
}

// Build shadow clip (per-cascade frustum-fit)
// Output:
//  clip.xy: [-1..1]
//  clip.z : [0..1] (D3D depth)
//  clip.w : 1
static float4 BuildShadowClipFromWorldPos(float3 worldPos)
{
	const uint c = g_ShadowCB.CascadeIndex;

	const CascadeAttribs C = GetCascadeAttribs(c);

	const float3 posLS = mul(float4(worldPos, 1.0), C.WorldToLightView).xyz;
	const float3 posProj = posLS * C.LightSpaceScale.xyz + C.LightSpaceScaledBias.xyz;

	return float4(posProj.xy, posProj.z, 1.0);
}


// ============================================================================
// Helpers
// ============================================================================
float3 PackNormal01(float3 n)
{
	return n * 0.5 + 0.5;
}

static float3 UnpackNormal01(float3 n01)
{
	return normalize(n01 * 2.0 - 1.0);
}

// Shading model encode/decode for UNORM8 channel
static float EncodeShadingModel_U8(uint id)
{
	// id in [0..255]
	return ((float) id + 0.5) / 255.0;
}

static uint DecodeShadingModel_U8(float enc)
{
	return (uint) floor(saturate(enc) * 255.0 + 0.5);
}

//------------------------------------------------------------------------------
// Clip / UV helpers
//------------------------------------------------------------------------------
float2 ClipToUV(float4 clip)
{
	float invW = rcp(max(abs(clip.w), 1e-6));
	float2 ndc = clip.xy * invW; // -1..1
	float2 uv;
	uv.x = ndc.x * 0.5 + 0.5;
	uv.y = 1.0 - (ndc.y * 0.5 + 0.5);
	return uv;
}

float3 ReconstructWorldPos(float2 uv, float depth01)
{
	float2 ndcXY = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
	float4 ndc = float4(ndcXY, depth01, 1.0);

	float4 ws = mul(ndc, g_FrameCB.InvViewProj);
	ws.xyz /= max(ws.w, 1e-6);
	return ws.xyz;
}

float3 ReconstructWorldRayDir(float2 uv)
{
	float2 ndcXY = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);

	float4 ndcNear = float4(ndcXY, 0.0, 1.0);
	float4 ndcFar = float4(ndcXY, 1.0, 1.0);

	float4 wsNear = mul(ndcNear, g_FrameCB.InvViewProj);
	float4 wsFar = mul(ndcFar, g_FrameCB.InvViewProj);

	wsNear.xyz /= max(wsNear.w, 1e-6);
	wsFar.xyz /= max(wsFar.w, 1e-6);

	return normalize(wsFar.xyz - wsNear.xyz);
}

uint2 SVPosToPixel(float4 svPos)
{
	return (uint2) floor(svPos.xy);
}

uint Hash_u32(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

float DitherThreshold4x4(int2 pix)
{
	static const float bayer4[16] =
	{
		0, 8, 2, 10,
		12, 4, 14, 6,
		3, 11, 1, 9,
		15, 7, 13, 5
	};

	int idx = (pix.x & 3) + ((pix.y & 3) << 2);
	return (bayer4[idx] + 0.5) / 16.0;
}

float GetBlueNoiseDither(int2 pix)
{
	float2 uv = float2(pix) / 1024.0;
	float2 offset = float2(g_FrameCB.FrameIndex * 0.618f, g_FrameCB.FrameIndex * 0.133f);
	return g_BlueNoiseTex.SampleLevel(g_PointWrapSampler, uv + offset, 0).r;
}

//------------------------------------------------------------------------------
// TAA Jitter (Halton)
//------------------------------------------------------------------------------
static const int MAX_HALTON_SEQUENCE = 16;

static const float2 HALTON_SEQUENCE[MAX_HALTON_SEQUENCE] =
{
	float2(0.5, 0.333333),
	float2(0.25, 0.666667),
	float2(0.75, 0.111111),
	float2(0.125, 0.444444),
	float2(0.625, 0.777778),
	float2(0.375, 0.222222),
	float2(0.875, 0.555556),
	float2(0.0625, 0.888889),
	float2(0.5625, 0.037037),
	float2(0.3125, 0.37037),
	float2(0.8125, 0.703704),
	float2(0.1875, 0.148148),
	float2(0.6875, 0.481482),
	float2(0.4375, 0.814815),
	float2(0.9375, 0.259259),
	float2(0.03125, 0.592593)
};

float4 ApplyTAAJittering(float4 clipSpace)
{
	int idx = (int) (g_FrameCB.FrameIndex % MAX_HALTON_SEQUENCE);

	float2 jitter = HALTON_SEQUENCE[idx];
	jitter.x = (jitter.x - 0.5f) / g_FrameCB.ViewportSize.x * 2.f;
	jitter.y = (jitter.y - 0.5f) / g_FrameCB.ViewportSize.y * 2.f;

	clipSpace.xy += jitter;
	return clipSpace;
}

float ClipPixelCoverage(float alpha, float4 svPos)
{
	float fade = saturate(svPos.z);
	float cutoff = lerp(0.5, 0.3, fade);
	float coverage = saturate((alpha - cutoff) / (1.0 - cutoff));
	float t = DitherThreshold4x4((int2) SVPosToPixel(svPos));
	clip(coverage - t);

	return coverage;
}

#endif // HLSL_COMMON_HLSLI
