#ifndef HLSL_VOLUMETRIC_FOG_COMMON_HLSLI
#define HLSL_VOLUMETRIC_FOG_COMMON_HLSLI

#include "Common.hlsli"

static const uint FOG_DOWNSAMPLE = 4;
static const uint FOG_Z_SLICES = 64;

cbuffer FOG_CONSTANTS
{
	FogConstants g_FogCB;
};

float GetCameraNear()
{
	return max(g_FrameCB.NearPlane, 1e-4);
}
float GetCameraFar()
{
	return min(g_FogCB.MaxDistance, g_FrameCB.FarPlane);
}

// Exponential distribution: viewZ(t) = near * (far/near)^t
float ViewZFromT(float t)
{
	float n = GetCameraNear();
	float f = GetCameraFar();
	return n * pow(f / n, saturate(t));
}

float TFromViewZ(float viewZ)
{
	float n = GetCameraNear();
	float f = GetCameraFar();
	return saturate(log(viewZ / n) / log(f / n));
}

// Reconstruct world pos from froxel uv + viewZ
float3 ReconstructWorldPosFromViewZ(float2 uv, float viewZ)
{
	float3 rayDirWS = ReconstructWorldRayDir(uv);
	float3 rayDirVS = mul(float4(rayDirWS, 0.0), g_FrameCB.View).xyz;

	float denom = max(rayDirVS.z, 1e-4);
	float distWS = viewZ / denom;

	return g_FrameCB.CameraPosition + rayDirWS * distWS;
}

// Henyey-Greenstein phase function (normalized)
float PhaseHG(float cosTheta, float g)
{
	float gg = g * g;
	float denom = pow(1.0 + gg - 2.0 * g * cosTheta, 1.5);
	return (1.0 - gg) / max(4.0 * PI * denom, 1e-6);
}

// Small hash for jitter/noise (deterministic)
uint HashU32(uint v)
{
	v ^= v >> 16;
	v *= 0x7feb352du;
	v ^= v >> 15;
	v *= 0x846ca68bu;
	v ^= v >> 16;
	return v;
}

float HashToUNorm01(uint v)
{
	return (HashU32(v) & 0x00FFFFFFu) / 16777215.0;
}

// Density model: homogeneous + optional height fog
float ComputeFogDensity(float3 worldPos)
{
	// Base homogeneous density (already in 1/m)
	float density = g_FogCB.BaseDensity * g_FogCB.DensityScale;

	// Optional height falloff (0 disables)
	if (g_FogCB.HeightFalloff > 0.0)
	{
		float h = worldPos.y - (g_FogCB.BaseHeight + g_FogCB.HeightFogStart);
		// Above start height -> exponential decay, below -> keep 1.0
		float heightFactor = (h > 0.0) ? exp(-h * g_FogCB.HeightFalloff) : 1.0;
		density *= heightFactor;
	}

	return max(density, 0.0);
}

// Returns [-0.5..0.5] jitter in pixels (you scale by strength and divide by resolution)
float2 GetHaltonJitter01(uint frameIndex)
{
	float2 h = HALTON_SEQUENCE[frameIndex & (MAX_HALTON_SEQUENCE - 1)];
	return (h - 0.5); // [-0.5..0.5]
}

// -----------------------------------------------------------------------------
// Interleaved gradient noise (stable, cheap) -> [0..1)
// Good for per-froxel jitter
// -----------------------------------------------------------------------------
float InterleavedGradientNoise(float2 pix, uint frame)
{
    // Common tiny hash; stable and fast
	float3 p = float3(pix, (float) frame);
	float n = frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453);
	return n;
}

// -----------------------------------------------------------------------------
// Neighborhood clamp (variance clip) for temporal stabilization
// - Compute min/max in a small neighborhood of CURRENT volume
// - Clamp history to [min,max] to prevent history from drifting/smearing
// -----------------------------------------------------------------------------
void GatherNeighborhoodMinMax_3x3x1(
    Texture3D<float4> tex,
    SamplerState samp,
    float3 uvw,
    float2 invWH,
    float invZ,
    out float3 outMinRGB,
    out float3 outMaxRGB,
    out float outMinA,
    out float outMaxA)
{
	outMinRGB = float3(1e9, 1e9, 1e9);
	outMaxRGB = float3(-1e9, -1e9, -1e9);
	outMinA = 1e9;
	outMaxA = -1e9;

    // 3x3 in XY, same Z (cheap + 효과 좋음)
    [unroll]
	for (int oy = -1; oy <= 1; ++oy)
	{
        [unroll]
		for (int ox = -1; ox <= 1; ++ox)
		{
			float3 u = uvw;
			u.x += (float) ox * invWH.x;
			u.y += (float) oy * invWH.y;

			float4 v = tex.SampleLevel(samp, u, 0);
			outMinRGB = min(outMinRGB, v.rgb);
			outMaxRGB = max(outMaxRGB, v.rgb);
			outMinA = min(outMinA, v.a);
			outMaxA = max(outMaxA, v.a);
		}
	}
}

float4 NeighborhoodClampHistory(
    float4 history,
    float3 minRGB, float3 maxRGB,
    float minA, float maxA,
    float clampExpand) // e.g. 0.02~0.08
{
    // Expand a little to avoid over-clamping (sparkle)
	float3 center = 0.5 * (minRGB + maxRGB);
	float3 halfR = 0.5 * (maxRGB - minRGB);
	halfR += clampExpand;

	float aCenter = 0.5 * (minA + maxA);
	float aHalf = 0.5 * (maxA - minA) + clampExpand;

	float3 lo = center - halfR;
	float3 hi = center + halfR;

	float alo = aCenter - aHalf;
	float ahi = aCenter + aHalf;

	float4 outv = history;
	outv.rgb = clamp(outv.rgb, lo, hi);
	outv.a = clamp(outv.a, alo, ahi);
	return outv;
}

#endif // HLSL_VOLUMETRIC_FOG_COMMON_HLSLI