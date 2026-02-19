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
// Diligent style: Alpha = perceptual^2, then some terms square again internally.
static float PerceptualToAlpha(float perceptualRoughness)
{
	perceptualRoughness = saturate(perceptualRoughness);
	perceptualRoughness = max(perceptualRoughness, MIN_ROUGHNESS);
	return perceptualRoughness * perceptualRoughness;
}

// Schlick Fresnel with explicit F90 (Diligent style)
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

// GGX normal distribution (D)
// Matches the stable form used in Diligent (a = alpha, and internally uses a^2 again).
static float D_GGX(float NdotH, float alpha)
{
	alpha = max(alpha, 1e-3);
	float a2 = alpha * alpha;
	float nh2 = NdotH * NdotH;
	float f = nh2 * a2 + (1.0 - nh2);
	return a2 / max(PI * f * f, 1e-9);
}

// Smith GGX correlated visibility term (Vis = G2 / (4 NoV NoL))
// Diligent: 0.5 / (NoL*sqrt(NoV^2(1-a2)+a2) + NoV*sqrt(NoL^2(1-a2)+a2))
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

// Lambert diffuse BRDF (with 1/PI)
static float3 Diffuse_Lambert_BRDF(float3 baseColor)
{
	return baseColor * (1.0 / PI);
}

// Hammon 2017 diffuse (as in your D3D12 sample), returns BRDF with 1/PI effectively accounted for
// In the original sample they omit 1/PI to cancel elsewhere; here we use it as a BRDF directly.
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
	// multi term from sample: 0.3641 * a  (where 0.3641 = PI * 0.1159)
	// Convert to BRDF-space: sample¡¯s diffuse omitted 1/PI; here we include 1/PI for consistency.
	// => (single + baseColor*multi) / PI
	float multi = 0.3641 * a2;

	return baseColor * (single + baseColor * multi) * (1.0 / PI);
}

// -----------------------------------------------------------------------------
// IBL (Split-Sum) - keep consistent Fresnel usage
// -----------------------------------------------------------------------------
static float3 EvaluateIBL_PBR(float3 N, float3 V, float3 baseColor, float metallic, float roughness, float ao)
{
	float NdotV = DotSat(N, V);

	// F0
	float3 F0 = lerp(0.04.xxx, baseColor, saturate(metallic));
	float3 F90 = 1.0.xxx;

	// For IBL, common choice: Fresnel at NdotV (not VdotH).
	float alpha = PerceptualToAlpha(roughness);
	float perceptualRoughness = max(saturate(roughness), MIN_ROUGHNESS);

	float3 F = FresnelSchlick_F0F90(F0, F90, NdotV);

	// Energy split (diffuse only for non-metals)
	float3 kd = (1.0 - F) * (1.0 - saturate(metallic));

	// Diffuse IBL (irradiance)
	float3 irradiance = g_IrradianceIBLTex.Sample(g_LinearClampSampler, N).rgb;
	float3 diffuseIBL = kd * baseColor * irradiance * (1.0 / PI);

	// Specular IBL (prefiltered env + BRDF LUT)
	float3 R = reflect(-V, N);

	uint w = 1, h = 1, mipLevels = 1;
	g_SpecularIBLTex.GetDimensions(0, w, h, mipLevels);

	// Most pipelines map perceptual roughness -> mip linearly.
	float maxMip = (float) max((int) mipLevels - 1, 0);
	float mip = perceptualRoughness * maxMip;

	float3 prefiltered = g_SpecularIBLTex.SampleLevel(g_LinearClampSampler, R, mip).rgb;

	// BRDF LUT is usually parameterized by (NdotV, roughness)
	float2 brdf = g_BrdfIBLTex.Sample(g_LinearClampSampler, float2(NdotV, perceptualRoughness));

	// split-sum: prefiltered * (F * brdf.x + brdf.y)
	float3 specIBL = prefiltered * (F * brdf.x + brdf.y);

	return (diffuseIBL + specIBL) * saturate(ao);
}

// -----------------------------------------------------------------------------
// Lighting function: direct PBR + IBL + emissive
// Notes:
// - shadow is expected 0..1 (0 fully shadowed)
// - iblScale lets you keep your current "ibl * 0.25" policy
// - signature MUST remain identical to your original
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
	// Normalize defensively (callers sometimes already do this, but cheap insurance).
	N = normalize(N);
	V = normalize(V);
	L = normalize(L);

	float NdotL = DotSat(N, L);
	float NdotV = DotSat(N, V);

	// If light is behind, you can early out to IBL+emissive
	// (still keep emissive + IBL visible)
	float3 ibl = EvaluateIBL_PBR(N, V, baseColor, metallic, roughness, ao) * iblScale;
	if (NdotL <= 0.0 || NdotV <= 0.0)
	{
		return ibl + emissive;
	}

	metallic = saturate(metallic);
	roughness = saturate(roughness);

	// F0
	float3 F0 = lerp(0.04.xxx, baseColor, metallic);
	float3 F90 = 1.0.xxx;

	// Half vector
	float3 H = normalize(V + L);
	float NdotH = DotSat(N, H);
	float VdotH = DotSat(V, H);

	// Microfacet params
	float alpha = PerceptualToAlpha(roughness);

	// Specular terms
	float D = D_GGX(NdotH, alpha);
	float Vis = Vis_SmithGGX_Correlated(NdotL, NdotV, alpha);
	float3 F = FresnelSchlick_F0F90(F0, F90, VdotH);

	float3 specBRDF = F * (D * Vis); // already includes 1/(4 NoL NoV) through Vis definition

	// Diffuse (choose one)
	// - Lambert is simplest and stable
	// - Hammon2017 gives nicer retroreflective / rough diffuse behavior
	float3 kd = (1.0 - F) * (1.0 - metallic);

	// Option A: Lambert
	// float3 diffBRDF = kd * Diffuse_Lambert_BRDF(baseColor);

	// Option B: Hammon2017 (recommended for direct lighting quality)
	float3 diffBRDF = kd * Diffuse_Hammon2017_BRDF(baseColor, roughness, N, V, L);

	// Radiance & shadow
	float3 radiance = lightColor * lightIntensity;

	// Direct lighting (no "NdotL + 0.1" fudge; use clean physics)
	float3 direct = (diffBRDF + specBRDF) * radiance * NdotL;

	// Apply shadow as visibility (0..1)
	direct *= saturate(shadow);

	// AO: already applied to IBL in EvaluateIBL_PBR. Usually NOT applied to direct.
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

    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffBRDF = kd * Diffuse_Hammon2017_BRDF(baseColor, roughness, N, V, L);

    float3 radiance = lightColor * lightIntensity;

    float3 direct = (diffBRDF + specBRDF) * radiance * NdotL;
    direct *= saturate(shadow);

    direct *= saturate(directScale);

    return ibl + direct + emissive;
}

#endif // PBR_LIGHTING_HLSLI
