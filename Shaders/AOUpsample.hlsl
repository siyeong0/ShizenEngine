// ============================================================================
// AOUpsample.hlsl
// Bilateral upsample from HALF-res AO -> FULL-res AO
// - Uses full-res depth/normal to preserve edges
// - 2x2 taps in half-res space (cheap)
// ============================================================================

#include "Common.hlsli"

Texture2D<float> g_AOHalf; // half-res AO (R16_FLOAT)
Texture2D<float> g_Depth; // full-res depth 0..1
Texture2D<float4> g_Normal; // full-res normal packed 0..1 (world)

RWTexture2D<float> g_OutAO; // full-res output AO (R16_FLOAT)

static const float SIGMA_DEPTH_UP = 0.0025;
static const float SIGMA_NDOT_UP = 0.15;

static float DepthWeight(float d0, float d1)
{
	float dd = abs(d0 - d1);
	return exp(-dd / max(SIGMA_DEPTH_UP, 1e-6f));
}

static float NormalWeight(float3 n0, float3 n1)
{
	float nd = saturate(dot(n0, n1));
	return exp(-(1.0f - nd) / max(SIGMA_NDOT_UP, 1e-6f));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 outSize;
	g_OutAO.GetDimensions(outSize.x, outSize.y);

	if (DTid.x >= outSize.x || DTid.y >= outSize.y)
		return;

	float2 uv = (float2(DTid.xy) + 0.5f) / float2(outSize);

	float centerDepth = g_Depth.SampleLevel(g_PointClampSampler, uv, 0);
	if (centerDepth >= 0.999999f)
	{
		g_OutAO[DTid.xy] = 1.0f;
		return;
	}

	float3 centerN = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uv, 0).xyz);

    // Map full-res uv -> half-res texel space
	uint2 halfSize;
	g_AOHalf.GetDimensions(halfSize.x, halfSize.y);

	float2 halfPos = uv * float2(halfSize) - 0.5f;
	int2 base = int2(floor(halfPos));
	float2 f = frac(halfPos);

    // 2x2 taps
	int2 p00 = clamp(base + int2(0, 0), int2(0, 0), int2(int(halfSize.x) - 1, int(halfSize.y) - 1));
	int2 p10 = clamp(base + int2(1, 0), int2(0, 0), int2(int(halfSize.x) - 1, int(halfSize.y) - 1));
	int2 p01 = clamp(base + int2(0, 1), int2(0, 0), int2(int(halfSize.x) - 1, int(halfSize.y) - 1));
	int2 p11 = clamp(base + int2(1, 1), int2(0, 0), int2(int(halfSize.x) - 1, int(halfSize.y) - 1));

	float2 uv00 = (float2(p00) + 0.5f) / float2(halfSize);
	float2 uv10 = (float2(p10) + 0.5f) / float2(halfSize);
	float2 uv01 = (float2(p01) + 0.5f) / float2(halfSize);
	float2 uv11 = (float2(p11) + 0.5f) / float2(halfSize);

    // AO taps (half-res)
	float ao00 = g_AOHalf.SampleLevel(g_PointClampSampler, uv00, 0);
	float ao10 = g_AOHalf.SampleLevel(g_PointClampSampler, uv10, 0);
	float ao01 = g_AOHalf.SampleLevel(g_PointClampSampler, uv01, 0);
	float ao11 = g_AOHalf.SampleLevel(g_PointClampSampler, uv11, 0);

    // Spatial weights (bilinear)
	float w00s = (1.0f - f.x) * (1.0f - f.y);
	float w10s = (f.x) * (1.0f - f.y);
	float w01s = (1.0f - f.x) * (f.y);
	float w11s = (f.x) * (f.y);

    // Bilateral weights: use full-res depth/normal sampled at tap uv (same uv)
	float d00 = g_Depth.SampleLevel(g_PointClampSampler, uv00, 0);
	float d10 = g_Depth.SampleLevel(g_PointClampSampler, uv10, 0);
	float d01 = g_Depth.SampleLevel(g_PointClampSampler, uv01, 0);
	float d11 = g_Depth.SampleLevel(g_PointClampSampler, uv11, 0);

	float3 n00 = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uv00, 0).xyz);
	float3 n10 = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uv10, 0).xyz);
	float3 n01 = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uv01, 0).xyz);
	float3 n11 = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uv11, 0).xyz);

	float w00 = w00s * DepthWeight(centerDepth, d00) * NormalWeight(centerN, n00);
	float w10 = w10s * DepthWeight(centerDepth, d10) * NormalWeight(centerN, n10);
	float w01 = w01s * DepthWeight(centerDepth, d01) * NormalWeight(centerN, n01);
	float w11 = w11s * DepthWeight(centerDepth, d11) * NormalWeight(centerN, n11);

	float sum = ao00 * w00 + ao10 * w10 + ao01 * w01 + ao11 * w11;
	float wsum = w00 + w10 + w01 + w11;

	float outAO = (wsum > 1e-6f) ? (sum / wsum) : (ao00 * w00s + ao10 * w10s + ao01 * w01s + ao11 * w11s);
	g_OutAO[DTid.xy] = outAO;
}