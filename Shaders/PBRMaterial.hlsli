#include "Common.hlsli"

#ifndef PBR_MATERIAL_HLSLI
#define PBR_MATERIAL_HLSLI

// Constant Buffers
cbuffer MATERIAL_CONSTANTS
{
	// BaseColor/Opacity
	float4 g_BaseColorFactor; // rgb = base color, a = opacity

	// Emissive
	float3 g_EmissiveFactor; // emissive rgb
	float g_EmissiveIntensity; // scalar

	// PBR factors
	float g_RoughnessFactor; // scalar
	float g_NormalScale; // scalar
	float g_OcclusionStrength; // scalar
	float g_AlphaCutoff; // scalar (for MASK)

	float g_MetallicFactor; // scalar
	uint g_MaterialFlags; // bitmask HAS_*
	uint g_ShadingMode;
	uint _pad0;
}

// Resources
Texture2D g_BaseColorTex;
Texture2D g_NormalTex;
Texture2D g_MetallicRoughnessTex;
Texture2D g_AOTex;
Texture2D g_EmissiveTex;
Texture2D g_HeightTex;

float3 GetBaseColor(float2 uv)
{
	float3 baseColor = g_BaseColorFactor.rgb;
	if ((g_MaterialFlags & MAT_HAS_BASECOLOR) != 0)
	{
		baseColor *= g_BaseColorTex.Sample(g_LinearWrapSampler, uv).rgb;
	}
	return baseColor;
}

float GetOpacity(float2 uv)
{
#ifndef MASKED
	return 1.0;
#else
	float alpha = g_BaseColorFactor.a;
	if ((g_MaterialFlags & MAT_HAS_BASECOLOR) != 0)
	{
		alpha *= g_BaseColorTex.Sample(g_LinearWrapSampler, uv).a;
	}
	return alpha;
#endif
}

float3 GetNormal(float2 uv, float3 normalWS, float3 tangentWS)
{
	float3 N = normalize(normalWS);
	if ((g_MaterialFlags & MAT_HAS_NORMAL) != 0)
	{
		float3 T = normalize(tangentWS);
		float3 B = normalize(cross(N, T));

		float3 nTS = UnpackNormal01(g_NormalTex.Sample(g_LinearWrapSampler, uv).xyz);
		nTS.xy *= g_NormalScale;

		float3x3 TBN = float3x3(T, B, N);
		N = normalize(mul(nTS, TBN));
	}
	return N;
}

float2 GetMetallicRoughness(float2 uv)
{
	float metallic = g_MetallicFactor;
	float roughness = g_RoughnessFactor;
	if ((g_MaterialFlags & MAT_HAS_MR) != 0)
	{
		float4 mr = g_MetallicRoughnessTex.Sample(g_LinearWrapSampler, uv);
		// glTF convention: B = metallic, G = roughness
		metallic *= mr.b;
		roughness *= mr.g;
	}
	return float2(metallic, roughness);
}

float GetAmbientOcclusion(float2 uv)
{
	float ao = 1.0;
	if ((g_MaterialFlags & MAT_HAS_AO) != 0)
	{
		ao = g_AOTex.Sample(g_LinearWrapSampler, uv).r;
		ao = lerp(1.0, ao, g_OcclusionStrength);
	}
	return ao;
}

float3 GetEmissive(float2 uv)
{
	float3 emissive = g_EmissiveFactor;
	if ((g_MaterialFlags & MAT_HAS_EMISSIVE) != 0)
	{
		emissive *= g_EmissiveTex.Sample(g_LinearWrapSampler, uv).rgb;
	}
	emissive *= g_EmissiveIntensity;
	return emissive;
}

uint GetShadingMode()
{
	return g_ShadingMode;
}

#endif //PBR_MATERIAL_HLSLI