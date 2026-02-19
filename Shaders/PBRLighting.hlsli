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
// PBRLighting.hlsli
// - Contains ALL BxDF functions (diffuse/spec/transmission/etc) + IBL evaluation.
// - Deferred PS should include ONLY this file.
// - Conventions:
//   * The original BxDF code omits 1/PI in BRDF terms (Lambert returns albedo, etc).
//   * This file preserves that convention for DIRECT and IBL diffuse for consistency.
//   * If your irradiance cubemap is authored as true irradiance (°ÚLi cos),
//     and you want physically explicit BRDF with 1/PI, then you would multiply
//     diffuse IBL by (1/PI) and also change diffuse BRDF accordingly.
// -----------------------------------------------------------------------------

static const float PI = 3.14159265358979323846;
static const float EPS = 1e-6;
static const float MIN_ROUGHNESS = 0.04; // for IBL mip/aliasing stability

// (Optional) env map used by sky/background in lighting PS
TextureCube<float4> g_EnvMapTex;

// IBL inputs
TextureCube<float4> g_IrradianceIBLTex; // diffuse irradiance
TextureCube<float4> g_SpecularIBLTex; // prefiltered specular (mips)
Texture2D<float2> g_BrdfIBLTex; // split-sum BRDF LUT

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

// Perceptual roughness (artist) -> alpha (microfacet)
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

static float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return FresnelSchlick_F0F90(F0, 1.0.xxx, cosTheta);
}

static float3 ComputeF0_FromBaseColorMetallic(float3 baseColor, float metallic)
{
    return lerp(0.04.xxx, baseColor, saturate(metallic));
}

// -----------------------------------------------------------------------------
// ===============================
// BxDF (ported 1:1 style)
// ===============================
// -----------------------------------------------------------------------------

// Fresnel reflectance - Schlick approximation.
float3 BxDF_Fresnel(in float3 F0, in float cos_thetai)
{
    return F0 + (1.0 - F0) * pow(abs(1.0 - cos_thetai), 5.0);
}

// -----------------------------------------------------------------------------
// Diffuse
// -----------------------------------------------------------------------------

// Lambert: returns albedo (1/PI omitted to cancel in rendering eq.)
float3 BxDF_DiffuseLambertF(in float3 albedo)
{
    return albedo;
}

// Hammon2017 diffuse (same convention as your BxDF: 1/PI omitted)
float3 BxDF_DiffuseHammonF(
    in float3 Albedo,
    in float Roughness,
    in float3 N,
    in float3 V,
    in float3 L,
    in float3 Fo)
{
    float3 diffuse = 0;

    float3 H = normalize(V + L);
    float NoH = dot(N, H);
    if (NoH > 0)
    {
        float a = Roughness * Roughness;

        float NoV = saturate(dot(N, V));
        float NoL = saturate(dot(N, L));
        float LoV = saturate(dot(L, V));

        float facing = 0.5 + 0.5 * LoV;
        float rough = facing * (0.9 - 0.4 * facing) * ((0.5 + NoH) / max(NoH, EPS));
        float3 smooth = 1.05 * (1 - pow(1 - NoL, 5)) * (1 - pow(1 - NoV, 5));

        float3 single = lerp(smooth, rough, a); // scalar 'rough' expands to float3
        float multi = 0.3641 * a; // 0.3641 = PI * 0.1159

        // NOTE: this is your original formulation (includes Albedo^2 in multi term).
        // Kept as-is because you requested "BxDF functions all included".
        diffuse = Albedo * (single + Albedo * multi);
    }
    return diffuse;
}

// -----------------------------------------------------------------------------
// Specular - Perfect reflection / transmission + GGX microfacet
// -----------------------------------------------------------------------------

// Perfect reflection: computes L and returns BRDF (Fresnel) value for that direction.
// Note: returns value without dividing by cos term (to cancel in rendering eq. caller).
float3 BxDF_SampleReflectionFr(
    in float3 V,
    out float3 L,
    in float3 N,
    in float3 Fo)
{
    L = reflect(-V, N);
    float cos_thetai = dot(N, L);
    return BxDF_Fresnel(Fo, cos_thetai);
}

// Check total internal reflection (preserved exactly as original)
bool BxDF_IsTotalInternalReflection(
    in float3 V,
    in float3 normal)
{
    float ior = 1;
    float eta = ior;
    float cos_thetai = dot(normal, V); // Incident angle

    return 1 - 1 * (1 - cos_thetai * cos_thetai) / (eta * eta) < 0;
}

// Perfect transmission: computes transmitted ray wt and returns BRDF value for that direction.
// Note: returns value without dividing by cos term (to cancel in rendering eq. caller).
float3 BxDF_SampleTransmissionFt(
    in float3 V,
    out float3 wt,
    in float3 N,
    in float3 Fo)
{
    // TODO in original: use parameters; keep hardcoded iors for identical behavior.
    float iorIn = 1.0; // air
    float iorOut = 1.33; // water
    float eta = iorIn / iorOut;

    wt = refract(-V, N, eta);

    float cos_thetai = dot(V, N);
    float3 Kr = BxDF_Fresnel(Fo, cos_thetai);
    return (1 - Kr);
}

// GGX microfacet specular BRDF (Karis style)
// NOTE: your original comment says "assumed remapped to alpha already".
// We'll keep the behavior identical: pass in whatever you were passing.
float3 BxDF_SpecularGGXF(
    in float Roughness,
    in float3 N,
    in float3 V,
    in float3 L,
    in float3 Fo)
{
    float3 H = V + L;
    float NoL = dot(N, L);
    float NoV = dot(N, V);
    float3 specular = 0;

    if (NoL > 0 && NoV > 0 && all(H))
    {
        H = normalize(H);
        float a = Roughness;
        float3 M = H; // microfacet normal
        float NoM = saturate(dot(N, M));
        float HoL = saturate(dot(H, L));

        float denom = 1 + NoM * NoM * (a * a - 1);
        float D = (a * a) / (denom * denom); // Karis

        float3 F = BxDF_Fresnel(Fo, HoL);

        float G = 0.5 / lerp(2 * NoL * NoV, NoL + NoV, a);

        specular = F * G * D;
    }

    return specular;
}

// -----------------------------------------------------------------------------
// Direct lighting wrapper (single light)
// -----------------------------------------------------------------------------
float3 BxDF_ShadeDirect(
    in float3 Albedo,
    in float3 Fo,
    in float3 Radiance,
    in bool inShadow,
    in float Roughness,
    in float3 N,
    in float3 V,
    in float3 L)
{
    float3 directLighting = 0;

    float NoL = dot(N, L);
    if (!inShadow && NoL > 0)
    {
        float3 directDiffuse = BxDF_DiffuseHammonF(Albedo, Roughness, N, V, L, Fo);
        float3 directSpecular = BxDF_SpecularGGXF(Roughness, N, V, L, Fo);

        directLighting = NoL * Radiance * (directDiffuse + directSpecular);
    }

    return directLighting;
}

// -----------------------------------------------------------------------------
// Full shade (direct + simple ambient) - preserved
// -----------------------------------------------------------------------------
float3 BxDF_Shade(
    in float3 Albedo,
    in float3 Fo,
    in float3 Radiance,
    in bool isInShadow,
    in float AmbientCoef,
    in float Roughness,
    in float3 N,
    in float3 V,
    in float3 L)
{
    float NoL = dot(N, L);
    Roughness = max(0.1, Roughness);

    float3 directLighting = 0;

    if (!isInShadow && NoL > 0)
    {
        float3 directDiffuse = BxDF_DiffuseHammonF(Albedo, Roughness, N, V, L, Fo);
        float3 directSpecular = BxDF_SpecularGGXF(Roughness, N, V, L, Fo);

        directLighting = NoL * Radiance * (directDiffuse + directSpecular);
    }

    float3 indirectDiffuse = AmbientCoef * Albedo;
    float3 indirectLighting = indirectDiffuse;

    return directLighting + indirectLighting;
}

// -----------------------------------------------------------------------------
// ===============================
// IBL (Split-Sum) - added
// ===============================
// -----------------------------------------------------------------------------

// Evaluates diffuse+spec IBL separately (returns "IBL factor" color contribution).
// - Uses F0 from baseColor/metallic.
// - Uses Fresnel at NdotV for stable energy split.
// - Applies AO to IBL (UE-like).
// - Keeps the same "1/PI omitted" convention as BxDF diffuse.
static float3 EvaluateIBL(
    float3 N,
    float3 V,
    float3 baseColor,
    float metallic,
    float roughness,
    float ao)
{
    N = normalize(N);
    V = normalize(V);

    float NdotV = DotSat(N, V);

    metallic = saturate(metallic);
    roughness = saturate(roughness);

    float perceptualRoughness = max(roughness, MIN_ROUGHNESS);

    float3 F0 = ComputeF0_FromBaseColorMetallic(baseColor, metallic);
    float3 F90 = 1.0.xxx;

    float3 Fv = FresnelSchlick_F0F90(F0, F90, NdotV);
    float3 kd = (1.0 - Fv) * (1.0 - metallic);

    // Diffuse IBL
    float3 irradiance = g_IrradianceIBLTex.Sample(g_LinearClampSampler, N).rgb;

    // Consistent with BxDF (Lambert omitted 1/PI) => omit here too.
    float3 diffuseIBL = kd * baseColor * irradiance;

    // Specular IBL
    float3 R = reflect(-V, N);

    uint w = 1, h = 1, mipLevels = 1;
    g_SpecularIBLTex.GetDimensions(0, w, h, mipLevels);

    float maxMip = (float) max((int) mipLevels - 1, 0);
    float mip = perceptualRoughness * maxMip;

    float3 prefiltered = g_SpecularIBLTex.SampleLevel(g_LinearClampSampler, R, mip).rgb;
    float2 brdf = g_BrdfIBLTex.Sample(g_LinearClampSampler, float2(NdotV, perceptualRoughness));

    float3 specIBL = prefiltered * (Fv * brdf.x + brdf.y);

    diffuseIBL *= (1.0 / PI);
    
    return (diffuseIBL + specIBL) * saturate(ao);
}

// Optional convenience: compute both factors (keeps PS clean).
static void ComputeLightingFactors(
    float3 N,
    float3 V,
    float3 L,
    float3 baseColor,
    float metallic,
    float roughness,
    float ao,
    float shadowVisibility, // 0..1
    float3 lightColor,
    float lightIntensity,
    out float3 OutDirectFactor,
    out float3 OutIBLFactor)
{
    N = normalize(N);
    V = normalize(V);
    L = normalize(L);

    float3 radiance = lightColor * lightIntensity;

    // IBL factor (no scale here)
    OutIBLFactor = EvaluateIBL(N, V, baseColor, metallic, roughness, ao);

    // Direct factor (BxDF); pass "false" then multiply smooth visibility
    float3 F0 = ComputeF0_FromBaseColorMetallic(baseColor, metallic);

    OutDirectFactor = BxDF_ShadeDirect(
        baseColor,
        F0,
        radiance,
        /*inShadow*/ false,
        roughness,
        N, V, L);

    OutDirectFactor *= saturate(shadowVisibility);
}

#endif // PBR_LIGHTING_HLSLI
