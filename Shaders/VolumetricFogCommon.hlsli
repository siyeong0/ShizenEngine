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
	return max(g_FrameCB.FarPlane, GetCameraNear() + 1.0);
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

#endif // HLSL_VOLUMETRIC_FOG_COMMON_HLSLI