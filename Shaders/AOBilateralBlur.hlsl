// ============================================================================
// AOBilateralBlur.hlsl
// Separable bilateral blur for HALF-res AO
// - g_Src: half-res AO (R16_FLOAT SRV)
// - g_Dst: half-res AO (R16_FLOAT UAV)
// - g_Depth/g_Normal: full-res (sample at same uv)
// ============================================================================

#include "Common.hlsli"

Texture2D<float> g_Src; // half-res AO
Texture2D<float> g_Depth; // full-res depth 0..1
Texture2D<float4> g_Normal; // full-res normal packed 0..1 (world)

RWTexture2D<float> g_Dst; // half-res output AO

// Tunables
static const int KERNEL_RADIUS = 2; // 5 taps
static const float SIGMA_SPATIAL = 1.25; // blur strength
static const float SIGMA_DEPTH = 0.0025; // depth edge sensitivity (tune)
static const float SIGMA_NDOT = 0.15; // normal edge sensitivity (tune)

static float Gaussian(float x, float sigma)
{
	return exp(-0.5f * (x * x) / max(sigma * sigma, 1e-6f));
}

static float NormalWeight(float3 n0, float3 n1)
{
	float nd = saturate(dot(n0, n1));
    // penalize when normals differ
	return exp(-(1.0f - nd) / max(SIGMA_NDOT, 1e-6f));
}

static float DepthWeight(float d0, float d1)
{
	float dd = abs(d0 - d1);
	return exp(-dd / max(SIGMA_DEPTH, 1e-6f));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 outSize;
	g_Dst.GetDimensions(outSize.x, outSize.y);

	if (DTid.x >= outSize.x || DTid.y >= outSize.y)
		return;

	float2 uv = (float2(DTid.xy) + 0.5f) / float2(outSize);

	float centerAO = g_Src.SampleLevel(g_PointClampSampler, uv, 0);

	float centerDepth = g_Depth.SampleLevel(g_PointClampSampler, uv, 0);
	float3 centerN = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uv, 0).xyz);

    // if sky, keep 1
	if (centerDepth >= 0.999999f)
	{
		g_Dst[DTid.xy] = 1.0f;
		return;
	}

	float sum = 0.0f;
	float wsum = 0.0f;

    // texel size in half-res
	float2 texel = 1.0f / float2(outSize);

#if defined(AO_BLUR_HORIZONTAL)
    int2 axis = int2(1, 0);
#elif defined(AO_BLUR_VERTICAL)
    int2 axis = int2(0, 1);
#else
	int2 axis = int2(1, 0);
#endif

    [unroll]
	for (int o = -KERNEL_RADIUS; o <= KERNEL_RADIUS; ++o)
	{
		int2 p = int2(DTid.xy) + axis * o;

		p.x = clamp(p.x, 0, int(outSize.x) - 1);
		p.y = clamp(p.y, 0, int(outSize.y) - 1);

		float2 suv = (float2(p) + 0.5f) / float2(outSize);

		float ao = g_Src.SampleLevel(g_PointClampSampler, suv, 0);

		float d = g_Depth.SampleLevel(g_PointClampSampler, suv, 0);
		float3 n = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, suv, 0).xyz);

		float ws = Gaussian(float(o), SIGMA_SPATIAL);
		float wd = DepthWeight(centerDepth, d);
		float wn = NormalWeight(centerN, n);

		float w = ws * wd * wn;

		sum += ao * w;
		wsum += w;
	}

	float outAO = (wsum > 1e-6f) ? (sum / wsum) : centerAO;
	g_Dst[DTid.xy] = outAO;
}