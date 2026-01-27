#ifndef PBR_LIGHTING_HLSLI
#define PBR_LIGHTING_HLSLI

static const float PI = 3.14159265358979323846;

SamplerState g_LinearClampSampler;

TextureCube<float4> g_EnvMapTex;
TextureCube<float4> g_IrradianceIBLTex;
TextureCube<float4> g_SpecularIBLTex;
Texture2D<float2> g_BrdfIBLTex;

static float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

static float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * Pow5(saturate(1.0 - cosTheta));
}

static float D_GGX(float NdotH, float roughness)
{
    float a = max(roughness, 0.04);
    float a2 = a * a;
    float a4 = a2 * a2;

    float d = (NdotH * NdotH) * (a4 - 1.0) + 1.0;
    return a4 / max(PI * d * d, 1e-7);
}

static float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-7);
}

static float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// ------------------------------------------------------------
// IBL evaluation must match your deferred lighting exactly.
// Expects these globals to exist in the shader that includes this file:
//   TextureCube<float4> g_IrradianceIBLTex;
//   TextureCube<float4> g_SpecularIBLTex;
//   Texture2D<float2>   g_BrdfIBLTex;
//   SamplerState        g_LinearClampSampler;
// ------------------------------------------------------------
static float3 EvaluateIBL_PBR(float3 N, float3 V, float3 baseColor, float metallic, float roughness, float ao)
{
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float NdotV = saturate(dot(N, V));

    float3 F = FresnelSchlick(F0, NdotV);
    float3 kd = (1.0 - F) * (1.0 - metallic);

    float3 irradiance = g_IrradianceIBLTex.Sample(g_LinearClampSampler, N).rgb;
    float3 diffuseIBL = kd * baseColor * irradiance;

    float3 R = reflect(-V, N);

    uint w = 1, h = 1, mipLevels = 1;
    g_SpecularIBLTex.GetDimensions(0, w, h, mipLevels);

    float mip = roughness * (float) (max((int) mipLevels - 1, 0));
    float3 prefiltered = g_SpecularIBLTex.SampleLevel(g_LinearClampSampler, R, mip).rgb;

    float2 brdf = g_BrdfIBLTex.Sample(g_LinearClampSampler, float2(NdotV, roughness));
    float3 specIBL = prefiltered * (F * brdf.x + brdf.y);

    return (diffuseIBL + specIBL) * ao;
}

// ------------------------------------------------------------
// Lighting function: direct PBR + IBL + emissive
// Notes:
// - shadow is expected 0..1 (0 fully shadowed)
// - iblScale lets you keep your current "ibl * 0.25" policy
// ------------------------------------------------------------
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
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 H = normalize(V + L);

    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(F0, VdotH);

    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffuseBRDF = kd * baseColor / PI;
    float3 specBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-5);

    float3 radiance = lightColor * lightIntensity;

    float3 direct = (diffuseBRDF + specBRDF) * radiance * NdotL;
    direct *= shadow;

    float3 ibl = EvaluateIBL_PBR(N, V, baseColor, metallic, roughness, ao);

    return ibl * iblScale + direct + emissive;
}

#endif // PBR_LIGHTING_HLSLI