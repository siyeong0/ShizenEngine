#include "Common.hlsli"

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

static float SampleShadow(
    Texture2D<float> shadowMap,
    SamplerComparisonState shadowCmpSampler,
    float3 worldPos)
{
	float3 shadowUVZ = WorldToShadowUVZ(worldPos, g_FrameCB.LightViewProj);
	
    // Bounds / far-plane early out
	if (shadowUVZ.x < 0.0 || shadowUVZ.x > 1.0 || shadowUVZ.y < 0.0 || shadowUVZ.y > 1.0)
	{
		return 1.0;
	}

	if (shadowUVZ.z >= 0.99)
	{
		return 1.0;
	}

	float depth = shadowUVZ.z;
	
    // Box PCF
	float sum = 0.0;
    [loop]
	for (int y = -1; y <= 1; ++y)
	{
        [loop]
		for (int x = -1; x <= 1; ++x)
		{
			float2 uv = shadowUVZ.xy + float2(x, y) * g_ShadowCB.InvViewportSize;
			sum += shadowMap.SampleCmpLevelZero(shadowCmpSampler, uv, depth);
		}
	}

	return sum / 9.0;
}

#endif // SHADOW_HLSLI