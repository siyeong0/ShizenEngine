#ifndef HLSL_COMMON_HLSLI
#define HLSL_COMMON_HLSLI

#include "HLSL_Structures.hlsli"

struct BaseVSInput
{
    float3 Position : ATTRIB0;
    float2 UV : ATTRIB1;
    float3 Normal : ATTRIB2;
    float3 Tangent : ATTRIB3;
};

#define GET_VERTEX_POS()		(IN.Position)
#define GET_VERTEX_UV()			(IN.UV)
#define GET_VERTEX_NORMAL()		(IN.Normal)
#define GET_VERTEX_TANGENT()	(IN.Tangent)

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

#define SET_VSOUT_WORLD_POS_STATIC(pos)                                 \
OUT.WorldPosition = (pos).xyz;                                          \
OUT.SVPosition = ApplyTAAJittering(mul((pos), g_ViewCB.ViewProj));      \
OUT.CurrClip = mul((pos), g_ViewCB.ViewProj);                           \
OUT.PrevClip = mul((pos), g_ViewCB.PrevViewProj);                     

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)							\
OUT.WorldPosition = (curr).xyz;											\
OUT.SVPosition = ApplyTAAJittering(mul((curr), g_ViewCB.ViewProj));     \
OUT.CurrClip = mul((curr), g_ViewCB.ViewProj);							\
OUT.PrevClip = mul((prev), g_ViewCB.PrevViewProj);

#define SET_VSOUT_UV(uv)            OUT.UV = (uv);
#define SET_VSOUT_WORLD_NORMAL(wn)  OUT.WorldNormal = (wn);
#define SET_VSOUT_WORLD_TANGENT(wt) OUT.WorldTangent = (wt);

#define SET_VSOUT_POS(svPos)        OUT.SVPosition = (svPos);
#define SET_VSOUT_CURR_CLIP(cc)     OUT.CurrClip = (cc);
#define SET_VSOUT_PREV_CLIP(pc)     OUT.PrevClip = (pc);

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

#define GET_PSIN_POS()				(IN.SVPosition)
#define GET_PSIN_CURR_CLIP()		(IN.CurrClip)
#define GET_PSIN_PREV_CLIP()		(IN.PrevClip)
#define GET_PSIN_UV()				(IN.UV)
#define GET_PSIN_WORLD_POS()		(IN.WorldPosition)
#define GET_PSIN_WORLD_NORMAL()		(IN.bFrontFace ? IN.WorldNormal : -IN.WorldNormal)
#define GET_PSIN_WORLD_TANGENT()	(IN.WorldTangent)
#define GET_PSIN_FRONTFACE()        (IN.bFrontFace)

struct BasePSOutput
{
    float4 GBuffer0 : SV_TARGET0; // Albedo.rgb, Opacity.a
    float4 GBuffer1 : SV_TARGET1; // Normal.xyz packed (0..1), unused or normal strength in w
    float4 GBuffer2 : SV_TARGET2; // Metallic, Roughness, AO, AlphaCoverage
    float4 GBuffer3 : SV_TARGET3; // Emissive.rgb, unused in a
    float2 Velocity : SV_TARGET4; // RG = motion vector (uv delta)
};

#define SET_PSOUT_ALBEDO(color)				OUT.GBuffer0.rgb = (color);
#define SET_PSOUT_OPACITY(opacity)			OUT.GBuffer0.a = (opacity);
#define SET_PSOUT_NORMAL_PACKED(normal)		OUT.GBuffer1 = float4((normal), 1.0);
#define SET_PSOUT_NORMAL(normal)			OUT.GBuffer1 = float4(PackNormal01((normal)), 1.0);
#define SET_PSOUT_METALLIC(metallic)		OUT.GBuffer2.r = (metallic);
#define SET_PSOUT_ROUGHNESS(roughness)		OUT.GBuffer2.g = (roughness);
#define SET_PSOUT_AO(ao)					OUT.GBuffer2.b = (ao);
#define SET_PSOUT_ALPHACOVERAGE(ac)			OUT.GBuffer2.a = (ac);
#define SET_PSOUT_EMISSIVE(emissive)		OUT.GBuffer3 = float4((emissive), 1.0);
#define SET_PSOUT_VELOCITY(velocity)		OUT.Velocity = (velocity);

#define BASE_VS_MAIN_ENTRY(INST_ID_VAR) void main(BaseVSInput IN, out BaseVSOutput OUT, uint INST_ID_VAR : SV_InstanceID)
#define BASE_PS_MAIN_ENTRY()			void main(in BasePSInput IN, out BasePSOutput OUT)

#else // DEPTH_ONLY

struct BaseVSOutput
{
	float4 SVPosition : SV_POSITION;

#ifdef MASKED
	float2 UV : TEXCOORD0;
#endif
};

#define SET_VSOUT_CURR_CLIP(cc)      /* no-op */
#define SET_VSOUT_PREV_CLIP(pc)      /* no-op */
#define SET_VSOUT_WORLD_NORMAL(wn)   /* no-op */
#define SET_VSOUT_WORLD_TANGENT(wt)  /* no-op */
#define SET_VSOUT_POS(svPos)        OUT.SVPosition = (svPos);

#ifdef MASKED

#define SET_VSOUT_WORLD_POS_STATIC(pos)		                            \
OUT.SVPosition = (mul((pos), g_ViewCB.ViewProj));

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)	                        \
OUT.SVPosition = (mul((curr), g_ViewCB.ViewProj));

#define SET_VSOUT_UV(uv)     OUT.UV = (uv);

#else // MASKED

#define SET_VSOUT_WORLD_POS_STATIC(pos)		                            \
OUT.SVPosition = (mul((pos), g_ViewCB.ViewProj));

#define SET_VSOUT_WORLD_POS_DYNAMIC(curr, prev)	                        \
OUT.SVPosition = (mul((curr), g_ViewCB.ViewProj));

#define SET_VSOUT_UV(uv)     /* no-op */

#endif

struct BasePSInput
{
	float4 SVPosition : SV_POSITION;

#ifdef MASKED
	float2 UV : TEXCOORD0;
#endif
};

#define GET_PSIN_POS()          (IN.SVPosition)

#ifdef MASKED
#define GET_PSIN_UV()           (IN.UV)
#else 
#define GET_PSIN_UV()           (float2(0.0, 0.0))
#endif

#define GET_PSIN_WORLD_POS()        (float3(0.0, 0.0, 0.0))  
#define GET_PSIN_CURR_CLIP()		(float4(0.0, 0.0, 0.0, 1.0))
#define GET_PSIN_PREV_CLIP()		(float4(0.0, 0.0, 0.0, 1.0))
#define GET_PSIN_WORLD_NORMAL()		(float3(0.0, 0.0, 1.0))
#define GET_PSIN_WORLD_TANGENT()	(float3(1.0, 0.0, 0.0))
#define GET_PSIN_FRONTFACE()        (false)


struct BasePSOutput { };

#define SET_PSOUT_ALBEDO(color)				/* no-op */
#define SET_PSOUT_OPACITY(opacity)			/* no-op */
#define SET_PSOUT_NORMAL_PACKED(normal)		/* no-op */
#define SET_PSOUT_NORMAL(normal)			/* no-op */
#define SET_PSOUT_METALLIC(metallic)		/* no-op */
#define SET_PSOUT_ROUGHNESS(roughness)		/* no-op */
#define SET_PSOUT_AO(ao)					/* no-op */
#define SET_PSOUT_ALPHACOVERAGE(ac)			/* no-op */
#define SET_PSOUT_EMISSIVE(emissive)		/* no-op */
#define SET_PSOUT_VELOCITY(velocity)		/* no-op */


#define BASE_VS_MAIN_ENTRY(INST_ID_VAR) void main(BaseVSInput IN, out BaseVSOutput OUT, uint INST_ID_VAR : SV_InstanceID)
#define BASE_PS_MAIN_ENTRY()            void main(in BasePSInput IN)

#endif // DEPTH_ONLY

// Objects
StructuredBuffer<ObjectConstants> g_ObjectTable;

// Constant Buffers
cbuffer FRAME_CONSTANTS
{
    FrameConstants g_FrameCB;
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

// Samplers
SamplerState g_LinearWrapSampler;
SamplerState g_LinearClampSampler;
SamplerState g_PointWrapSampler;
SamplerState g_PointClampSampler;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
float3 UnpackNormalTS(float3 n01)
{
    return normalize(n01 * 2.0 - 1.0);
}

float3 PackNormal01(float3 n)
{
    return n * 0.5 + 0.5;
}

//------------------------------------------------------------------------------
// Clip / UV helpers
//------------------------------------------------------------------------------
float2 ClipToUV(float4 clip)
{
    float2 ndc = clip.xy / max(clip.w, 1e-6); // [-1..1]
    return ndc * 0.5 + 0.5; // [0..1]
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