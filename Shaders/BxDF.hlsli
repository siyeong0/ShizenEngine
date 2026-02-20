#include "Common.hlsli"

#ifndef BXDF_HLSLI
#define BXDF_HLSLI

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
static const float PI = 3.14159265358979323846;
static const float EPS = 1e-6;
static const float MIN_ROUGHNESS = 0.04; // for IBL mip/aliasing stability

//-----------------------------------------------------------------------------
// Textures (as provided)
//-----------------------------------------------------------------------------
// (Optional) env map used by sky/background in lighting PS
TextureCube<float4> g_EnvMapTex;

// IBL inputs
TextureCube<float4> g_IrradianceIBLTex; // diffuse irradiance
TextureCube<float4> g_SpecularIBLTex; // prefiltered specular (mips)
Texture2D<float2> g_BrdfIBLTex; // split-sum BRDF LUT

//-----------------------------------------------------------------------------
// Tunables (UE-ish defaults)
//-----------------------------------------------------------------------------
#ifndef BXDF_DEFAULT_SPECULAR
// UE "Specular" default is 0.5 -> dielectric F0 = 0.08*0.5 = 0.04
#define BXDF_DEFAULT_SPECULAR 0.5
#endif

#ifndef BXDF_INDIRECT_DIFFUSE_AO_STRENGTH
#define BXDF_INDIRECT_DIFFUSE_AO_STRENGTH 1.0
#endif

#ifndef BXDF_INDIRECT_SPEC_AO_STRENGTH
#define BXDF_INDIRECT_SPEC_AO_STRENGTH 1.0
#endif

#ifndef BXDF_IBL_INTENSITY
#define BXDF_IBL_INTENSITY 1.0
#endif

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------
static float Saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}
static float3 Saturate3(float3 v)
{
    return clamp(v, 0.0, 1.0);
}

static float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

// UE-style F0 from BaseColor/Metallic/(Specular default 0.5)
static float3 ComputeF0_UE(float3 baseColor, float metallic, float specular)
{
    float dielectric = 0.08 * Saturate(specular); // UE convention
    float3 F0_dielectric = float3(dielectric, dielectric, dielectric);
    return lerp(F0_dielectric, baseColor, Saturate(metallic));
}

static float3 Fresnel_Schlick(float3 F0, float VoH)
{
    return F0 + (1.0 - F0) * Pow5(1.0 - Saturate(VoH));
}

// GGX NDF: uses a2 = (alpha^2), where alpha = roughness^2 (UE convention)
static float D_GGX(float NoH, float a2)
{
    float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-8);
}

// UE-style Smith-Schlick GGX visibility term
// k = (roughness + 1)^2 / 8  (roughness = perceptual)
static float G_SchlickGGX_UE(float NoX, float k)
{
    return NoX / (NoX * (1.0 - k) + k);
}

static float G_Smith_UE(float NoV, float NoL, float k)
{
    return G_SchlickGGX_UE(NoV, k) * G_SchlickGGX_UE(NoL, k);
}

// Burley diffuse (Disney 2012 / UE-ish)
static float3 Diffuse_Burley(float3 diffuseColor, float roughness, float NoV, float NoL, float VoH)
{
    float r = Saturate(roughness);

    // Disney diffuse model
    float Fd90 = 0.5 + 2.0 * r * VoH * VoH;

    float lightScatter = 1.0 + (Fd90 - 1.0) * Pow5(1.0 - NoL);
    float viewScatter = 1.0 + (Fd90 - 1.0) * Pow5(1.0 - NoV);

    return (diffuseColor / PI) * lightScatter * viewScatter;
}

//-----------------------------------------------------------------------------
// IBL helpers
//-----------------------------------------------------------------------------
static float ComputeSpecularIBLMipFromRoughness(float perceptualRoughness)
{
    // Clamp for stability (avoid super-sharp aliasing unless you truly have very high-res env)
    float r = max(Saturate(perceptualRoughness), MIN_ROUGHNESS);

    // Query mip count
    uint w, h, mipCount;
    // TextureCube.GetDimensions signature varies slightly across compilers; this works in most HLSL:
    g_SpecularIBLTex.GetDimensions(0, w, h, mipCount);

    // Map roughness -> mip (UE-like: linear in roughness * (mipCount-1))
    float maxMip = max((float) mipCount - 1.0, 0.0);
    return r * maxMip;
}

static float3 SampleIrradianceIBL(float3 N)
{
    return g_IrradianceIBLTex.Sample(g_LinearClampSampler, N).rgb;
}

static float3 SampleSpecularIBL(float3 R, float roughness)
{
    float mip = ComputeSpecularIBLMipFromRoughness(roughness);
    return g_SpecularIBLTex.SampleLevel(g_LinearClampSampler, R, mip).rgb;
}

static float2 SampleBRDFLUT(float NoV, float roughness)
{
    float2 uv = float2(Saturate(NoV), Saturate(roughness));
    return g_BrdfIBLTex.Sample(g_LinearClampSampler, uv);
}

//-----------------------------------------------------------------------------
// Direct lighting (UE-ish): Burley Diffuse + GGX Spec
// Signature you asked for (direct-only result, already NoL-weighted and shadowed)
//-----------------------------------------------------------------------------
static float3 ShadeDirect(
    float3 N,
    float3 V, // surface->camera
    float3 L, // surface->light
    float3 LightRadiance, // light color * intensity (linear radiance)
    float3 baseColor,
    float metallic,
    float roughness,
    float shadow)           // 0..1
{
    N = normalize(N);
    V = normalize(V);
    L = normalize(L);

    float3 H = normalize(V + L);

    float NoV = Saturate(dot(N, V));
    float NoL = Saturate(dot(N, L));
    float NoH = Saturate(dot(N, H));
    float VoH = Saturate(dot(V, H));

    if (NoL <= 0.0 || NoV <= 0.0)
        return 0.0;

    float r = Saturate(roughness);

    // UE roughness mapping: alpha = r^2, a2 = alpha^2
    float a = max(r * r, 1e-4);
    float a2 = a * a;

    float3 F0 = ComputeF0_UE(baseColor, metallic, BXDF_DEFAULT_SPECULAR);
    float3 F = Fresnel_Schlick(F0, VoH);

    // Specular (Cook-Torrance)
    float D = D_GGX(NoH, a2);
    float k = ((r + 1.0) * (r + 1.0)) / 8.0;
    float G = G_Smith_UE(NoV, NoL, k);

    float3 spec = (D * G * F) / max(4.0 * NoV * NoL, 1e-6);

    // Diffuse (Burley) + energy conservation
    float3 diffuseColor = baseColor * (1.0 - metallic);

    // UE-ish: kD = 1 - F (per-channel) to conserve energy w.r.t. Fresnel
    float3 kD = (1.0 - F);

    float3 diff = Diffuse_Burley(diffuseColor, r, NoV, NoL, VoH) * kD;

    float3 direct = (diff + spec) * LightRadiance * NoL;

    // Apply shadow to direct only
    direct *= Saturate(shadow);

    return direct;
}

//-----------------------------------------------------------------------------
// Indirect lighting (UE-ish split-sum IBL)
// - AO usually affects indirect; keep it here
//-----------------------------------------------------------------------------
static float3 ShadeIndirectIBL(
    float3 N,
    float3 V,
    float3 baseColor,
    float metallic,
    float roughness,
    float ao)
{
    N = normalize(N);
    V = normalize(V);

    float NoV = Saturate(dot(N, V));
    float r = max(Saturate(roughness), MIN_ROUGHNESS);

    float3 F0 = ComputeF0_UE(baseColor, metallic, BXDF_DEFAULT_SPECULAR);

    // Diffuse IBL
    float3 diffuseColor = baseColor * (1.0 - metallic);
    float3 irradiance = SampleIrradianceIBL(N);
    float3 diffuseIBL = irradiance * (diffuseColor / PI);

    // Specular IBL
    float3 R = reflect(-V, N);
    float3 prefiltered = SampleSpecularIBL(R, r);
    float2 brdf = SampleBRDFLUT(NoV, r);

    // UE-ish split-sum
    float3 specIBL = prefiltered * (F0 * brdf.x + brdf.y);

    // AO application (simple knobs)
    float aoSat = Saturate(ao);
    float diffuseAO = lerp(1.0, aoSat, Saturate(BXDF_INDIRECT_DIFFUSE_AO_STRENGTH));
    float specAO = lerp(1.0, aoSat, Saturate(BXDF_INDIRECT_SPEC_AO_STRENGTH));

    float3 indirect = diffuseIBL * diffuseAO + specIBL * specAO;
    indirect *= BXDF_IBL_INTENSITY;

    return indirect;
}

//-----------------------------------------------------------------------------
// Combined shading: direct + indirect
// Your requested signature (+ ao + shadow)
//-----------------------------------------------------------------------------
static float3 Shade(
    float3 N,
    float3 V,
    float3 L,
    float3 LightRadiance,
    float3 baseColor,
    float metallic,
    float roughness,
    float ao,
    float shadow)
{
    float3 direct = ShadeDirect(N, V, L, LightRadiance, baseColor, metallic, roughness, shadow);
    float3 indirect = ShadeIndirectIBL(N, V, baseColor, metallic, roughness, ao);
    return direct + indirect;
}

#endif // BXDF_HLSLI