#ifndef SHADOW_HLSLI
#define SHADOW_HLSLI

static float3 WorldToShadowUVZ(float3 worldPos, float4x4 lightViewProj)
{
	float4 clip = mul(float4(worldPos, 1.0), lightViewProj);
	clip.xyz /= max(clip.w, 1e-6);

	float2 uv = clip.xy * 0.5 + 0.5;
	uv.y = 1.0 - uv.y;

	return float3(uv, clip.z);
}

// -----------------------------------------------------------------------------
// Single-tap shadow compare (no PCF)
// -----------------------------------------------------------------------------
float SampleShadow(
    Texture2D<float> shadowMap,
    SamplerComparisonState shadowCmpSampler,
    float3 shadowUVZ,
    float depthBias)
{
    // Bounds / far-plane early out
	if (shadowUVZ.x < 0.0 || shadowUVZ.x > 1.0 || shadowUVZ.y < 0.0 || shadowUVZ.y > 1.0)
		return 1.0;

    // If your shadow map stores depth in [0..1] and 1.0 means far, keep this.
	if (shadowUVZ.z >= 1.0)
		return 1.0;

	float depth = shadowUVZ.z - depthBias;

    // Compare sampler does: (depth <= shadowDepth) ? 1 : 0 with HW filtering rules.
	return shadowMap.SampleCmpLevelZero(shadowCmpSampler, shadowUVZ.xy, depth);
}

#endif // SHADOW_HLSLI