//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "Common.hlsli"

#ifndef PBR_LIGHTING_HLSLI
#define PBR_LIGHTING_HLSLI

// -----------------------------------------------------------------------------
// PBR Lighting (Direct + IBL) - upgraded
//
// Goals (based on Diligent PBR + D3D12 sample style):
// - Use GGX NDF + Smith GGX *correlated* visibility (stable & common in engines)
// - Use Schlick Fresnel with F90
// - Better diffuse (Hammon 2017) option for direct light (less "flat" than pure Lambert)
// - IBL uses split-sum (prefiltered spec cubemap + BRDF LUT) with consistent Fresnel
// - Keep your Shade() signature EXACTLY as-is
//
// Requirements (same as your original file):
//   TextureCube<float4> g_IrradianceIBLTex;
//   TextureCube<float4> g_SpecularIBLTex;
//   Texture2D<float2>   g_BrdfIBLTex;
//   SamplerState        g_LinearClampSampler;
// -----------------------------------------------------------------------------

static const float PI = 3.14159265358979323846;
static const float EPS = 1e-6;
static const float MIN_ROUGHNESS = 0.04; // common practical floor to avoid fireflies / extreme spec

TextureCube<float4> g_EnvMapTex; // (optional) not used directly here, kept for compatibility
TextureCube<float4> g_IrradianceIBLTex; // diffuse IBL
TextureCube<float4> g_SpecularIBLTex; // prefiltered spec IBL (mip chain = roughness)
Texture2D<float2> g_BrdfIBLTex; // split-sum BRDF LUT (NdotV, roughness)

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------
static float Pow5(float x)
{
	float x2 = x * x;
	return x2 * x2 * x;
}

static float3 Pow5(float3 x)
{
	float3 x2 = x * x;
	return x2 * x2 * x;
}

static float DotSat(float3 a, float3 b)
{
	return saturate(dot(a, b));
}

// Perceptual roughness (artist) -> alpha roughness (microfacet)
static float PerceptualToAlpha(float perceptualRoughness)
{
	perceptualRoughness = saturate(perceptualRoughness);
	perceptualRoughness = max(perceptualRoughness, MIN_ROUGHNESS);
	return perceptualRoughness * perceptualRoughness;
}

// Schlick Fresnel with explicit F90
static float3 FresnelSchlick_F0F90(float3 F0, float3 F90, float cosTheta)
{
	return F0 + (F90 - F0) * Pow5(saturate(1.0 - cosTheta));
}

// Convenience Fresnel when you only have F0 (assume F90 = 1)
static float3 FresnelSchlick(float3 F0, float cosTheta)
{
	return FresnelSchlick_F0F90(F0, 1.0.xxx, cosTheta);
}

// -----------------------------------------------------------------------------
// GGX Terms (D, Vis)
// -----------------------------------------------------------------------------
static float D_GGX(float NdotH, float alpha)
{
	alpha = max(alpha, 1e-3);
	float a2 = alpha * alpha;
	float nh2 = NdotH * NdotH;
	float f = nh2 * a2 + (1.0 - nh2);
	return a2 / max(PI * f * f, 1e-9);
}

// Smith GGX correlated visibility term
static float Vis_SmithGGX_Correlated(float NdotL, float NdotV, float alpha)
{
	float a2 = alpha * alpha;

	float GGXV = NdotL * sqrt(max(NdotV * NdotV * (1.0 - a2) + a2, 1e-7));
	float GGXL = NdotV * sqrt(max(NdotL * NdotL * (1.0 - a2) + a2, 1e-7));

	return 0.5 / max(GGXV + GGXL, 1e-7);
}

// -----------------------------------------------------------------------------
// Diffuse models
// -----------------------------------------------------------------------------
static float3 Diffuse_Lambert_BRDF(float3 baseColor)
{
	return baseColor * (1.0 / PI);
}

// Hammon 2017 diffuse (direct lighting)
// NOTE: Returns BRDF in "per-steradian" space (includes 1/PI here).
static float3 Diffuse_Hammon2017_BRDF(
	float3 baseColor,
	float perceptualRoughness,
	float3 N,
	float3 V,
	float3 L)
{
	float3 H = normalize(V + L);
	float NoH = DotSat(N, H);
	if (NoH <= 0.0)
		return 0.0.xxx;

	float NoV = DotSat(N, V);
	float NoL = DotSat(N, L);
	float LoV = DotSat(L, V);

	float a = saturate(perceptualRoughness);
	float a2 = a * a;

	float facing = 0.5 + 0.5 * LoV;
	float rough = facing * (0.9 - 0.4 * facing) * ((0.5 + NoH) / max(NoH, EPS));
	float3 smooth = 1.05 * (1.0 - Pow5(1.0 - NoL)) * (1.0 - Pow5(1.0 - NoV));

	float3 single = lerp(smooth, rough.xxx, a2);

	// Multi scattering approximation (sample-derived term).
	// IMPORTANT FIX:
	// Do NOT multiply baseColor twice (prevents baseColor^2 muddy darkening).
	float multi = 0.3641 * a2;

	return baseColor * (single + multi) * (1.0 / PI);
}

// -----------------------------------------------------------------------------
// IBL (Split-Sum)
// -----------------------------------------------------------------------------
static float3 EvaluateIBL_PBR(float3 N, float3 V, float3 baseColor, float metallic, float roughness, float ao)
{
	float NdotV = DotSat(N, V);

	float3 F0 = lerp(0.04.xxx, baseColor, saturate(metallic));
	float3 F90 = 1.0.xxx;

	float alpha = PerceptualToAlpha(roughness);
	float perceptualRoughness = max(saturate(roughness), MIN_ROUGHNESS);

	// For IBL, use Fresnel at NdotV (common & stable)
	float3 F = FresnelSchlick_F0F90(F0, F90, NdotV);

	float3 kd = (1.0 - F) * (1.0 - saturate(metallic));

	// Diffuse IBL (irradiance)
	float3 irradiance = g_IrradianceIBLTex.Sample(g_LinearClampSampler, N).rgb;

	// NOTE:
	// This assumes irradiance is true irradiance (¡òLi cos) and Lambert BRDF needs 1/PI.
	// If your irradiance cubemap already includes 1/PI, remove *(1.0/PI) here.
	float3 diffuseIBL = kd * baseColor * irradiance * (1.0 / PI);

	// Specular IBL (prefiltered env + BRDF LUT)
	float3 R = reflect(-V, N);

	uint w = 1, h = 1, mipLevels = 1;
	g_SpecularIBLTex.GetDimensions(0, w, h, mipLevels);

	float maxMip = (float) max((int) mipLevels - 1, 0);
	float mip = perceptualRoughness * maxMip;

	float3 prefiltered = g_SpecularIBLTex.SampleLevel(g_LinearClampSampler, R, mip).rgb;

	float2 brdf = g_BrdfIBLTex.Sample(g_LinearClampSampler, float2(NdotV, perceptualRoughness));

	float3 specIBL = prefiltered * (F * brdf.x + brdf.y);

	return (diffuseIBL + specIBL) * saturate(ao);
}

// -----------------------------------------------------------------------------
// Lighting function: direct PBR + IBL + emissive
// - shadow: 0..1 visibility
// - iblScale: your policy (e.g. 0.25)
// Signature must match your usage.
// -----------------------------------------------------------------------------
static float3 Shade(
	float3 N,
	float3 V,
	float3 L,
	float3 baseColor,
	float metallic,
	float roughness,
	float ao,
	float3 emissive,
	float shadow,
	float3 lightColor,
	float lightIntensity,
	float iblScale)
{
	N = normalize(N);
	V = normalize(V);
	L = normalize(L);

	float NdotL = DotSat(N, L);
	float NdotV = DotSat(N, V);

	// IBL (AO applied inside)
	float3 ibl = EvaluateIBL_PBR(N, V, baseColor, metallic, roughness, ao) * iblScale;

	if (NdotL <= 0.0 || NdotV <= 0.0)
	{
		return ibl + emissive;
	}

	metallic = saturate(metallic);
	roughness = saturate(roughness);

	float3 F0 = lerp(0.04.xxx, baseColor, metallic);
	float3 F90 = 1.0.xxx;

	float3 H = normalize(V + L);
	float NdotH = DotSat(N, H);
	float VdotH = DotSat(V, H);

	float alpha = PerceptualToAlpha(roughness);

	// Specular BRDF
	float D = D_GGX(NdotH, alpha);
	float Vis = Vis_SmithGGX_Correlated(NdotL, NdotV, alpha);
	float3 F = FresnelSchlick_F0F90(F0, F90, VdotH);

	float3 specBRDF = F * (D * Vis);

	// Diffuse energy split:
	// IMPORTANT FIX:
	// Use Fresnel at NdotV for kd (stable, avoids view-dependent diffuse wobble).
	float3 Fd = FresnelSchlick_F0F90(F0, F90, NdotV);
	float3 kd = (1.0 - Fd) * (1.0 - metallic);

	// Diffuse BRDF
	float3 diffBRDF = kd * Diffuse_Hammon2017_BRDF(baseColor, roughness, N, V, L);

	float3 radiance = lightColor * lightIntensity;

	float3 direct = (diffBRDF + specBRDF) * radiance * NdotL;
	direct *= saturate(shadow);

	// AO is intentionally NOT applied to direct (UE-like)
	return ibl + direct + emissive;
}

static float3 Shade_ScaleDirectOnly(
	float3 N,
	float3 V,
	float3 L,
	float3 baseColor,
	float metallic,
	float roughness,
	float ao,
	float3 emissive,
	float shadow,
	float3 lightColor,
	float lightIntensity,
	float iblScale,
	float directScale)
{
	N = normalize(N);
	V = normalize(V);
	L = normalize(L);

	float NdotL = DotSat(N, L);
	float NdotV = DotSat(N, V);

	float3 ibl = EvaluateIBL_PBR(N, V, baseColor, metallic, roughness, ao) * iblScale;

	if (NdotL <= 0.0 || NdotV <= 0.0)
	{
		return ibl + emissive;
	}

	metallic = saturate(metallic);
	roughness = saturate(roughness);

	float3 F0 = lerp(0.04.xxx, baseColor, metallic);
	float3 F90 = 1.0.xxx;

	float3 H = normalize(V + L);
	float NdotH = DotSat(N, H);
	float VdotH = DotSat(V, H);

	float alpha = PerceptualToAlpha(roughness);

	float D = D_GGX(NdotH, alpha);
	float Vis = Vis_SmithGGX_Correlated(NdotL, NdotV, alpha);
	float3 F = FresnelSchlick_F0F90(F0, F90, VdotH);

	float3 specBRDF = F * (D * Vis);

	// Same kd fix as Shade()
	float3 Fd = FresnelSchlick_F0F90(F0, F90, NdotV);
	float3 kd = (1.0 - Fd) * (1.0 - metallic);

	float3 diffBRDF = kd * Diffuse_Hammon2017_BRDF(baseColor, roughness, N, V, L);

	float3 radiance = lightColor * lightIntensity;

	float3 direct = (diffBRDF + specBRDF) * radiance * NdotL;
	direct *= saturate(shadow);
	direct *= saturate(directScale);

	return ibl + direct + emissive;
}

#endif // PBR_LIGHTING_HLSLI