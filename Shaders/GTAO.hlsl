// ============================================================================
// GTAO_CS.hlsl
// Half-resolution GTAO (Compute)
// - View-space horizon search
// - 4 directions x 4 steps (interleaving/temporal은 다음 단계)
// - Writes AO in R16_FLOAT UAV (0..1, 1=unoccluded)
// ============================================================================

#include "Common.hlsli"

Texture2D<float> g_GBufferDepth; // full-res depth 0..1
Texture2D<float4> g_GBufferNormal; // full-res packed 0..1 (WORLD)

RWTexture2D<float> g_OutAO; // half-res R16_FLOAT (bound as UAV)

// Tunables
static const float GTAO_RADIUS_WS = 1.5;
static const int GTAO_NUM_DIRECTIONS = 4; // reduced
static const int GTAO_NUM_STEPS = 4; // reduced

static const float GTAO_BIAS_WS = 0.01;
static const float GTAO_MIN_HORIZON_COS = 0.18;
static const float GTAO_POWER = 2.55;
static const float GTAO_INTENSITY = 2.25;

static const float GTAO_MAX_DIST_WS = 3.0;
static const float GTAO_DEPTH_EPS = 1e-5;

// Helpers
static bool IsInsideScreen(float2 uv)
{
	return all(uv >= 0.0f) && all(uv <= 1.0f);
}

static void BuildOrthoBasis(float3 N, out float3 T, out float3 B)
{
	float3 up = (abs(N.y) < 0.999f) ? float3(0, 1, 0) : float3(1, 0, 0);
	T = normalize(cross(up, N));
	B = cross(N, T);
}

static float2 Rotate2D(float2 v, float2 rotUnit)
{
	return float2(
        v.x * rotUnit.x - v.y * rotUnit.y,
        v.x * rotUnit.y + v.y * rotUnit.x
    );
}

static float3 WorldNormalToView(float3 Nws)
{
	float3 Nvs = mul(float4(Nws, 0.0f), g_FrameCB.View).xyz;
	return normalize(Nvs);
}

static float3 ReconstructViewPos(float2 uv, float depth01)
{
	float2 ndc;
	ndc.x = uv.x * 2.0f - 1.0f;
	ndc.y = 1.0f - uv.y * 2.0f;

	float4 clip = float4(ndc.x, ndc.y, depth01, 1.0f);
	float4 view = mul(clip, g_FrameCB.InvProj);
	view.xyz /= max(view.w, 1e-6f);
	return view.xyz;
}

static float4 ViewToClip(float3 vs)
{
	return mul(float4(vs, 1.0f), g_FrameCB.Proj);
}

static float EvaluateHorizonOneDirVS(
    float2 baseUV,
    float baseDepth01,
    float3 Pvs,
    float3 Nvs,
    float3 dirVS,
    float noise01)
{
	float radiusWS = GTAO_RADIUS_WS;

	float3 endVS = Pvs + dirVS * radiusWS;
	float4 endClip = ViewToClip(endVS);
	if (endClip.w <= 1e-6f)
		return 0.0f;

	float2 endUV = ClipToUV(endClip);
	float2 deltaUV = endUV - baseUV;

	float deltaLen = length(deltaUV);
	if (deltaLen < 1e-6f)
		return 0.0f;

	float3 startVS = Pvs + Nvs * GTAO_BIAS_WS;

	float horizon = -1.0f;

    [unroll]
	for (int i = 1; i <= GTAO_NUM_STEPS; ++i)
	{
		float t01 = (float(i) - noise01) * (1.0f / float(GTAO_NUM_STEPS));
		t01 = saturate(t01);

		float2 suv = baseUV + deltaUV * t01;
		if (!IsInsideScreen(suv))
			break;

		float sceneDepth01 = g_GBufferDepth.SampleLevel(g_PointClampSampler, suv, 0);
		if (sceneDepth01 >= 0.999999f)
			continue;

        // only occluders closer than receiver
		if (sceneDepth01 >= baseDepth01 - GTAO_DEPTH_EPS)
			continue;

		float3 sceneVS = ReconstructViewPos(suv, sceneDepth01);

		float3 Vvs = sceneVS - startVS;
		float d2 = dot(Vvs, Vvs);
		if (d2 < 1e-8f)
			continue;

		float dist = sqrt(d2);

		float maxDist = min(GTAO_MAX_DIST_WS, radiusWS * 1.25f);
		if (dist > maxDist)
			continue;

		float3 vDir = Vvs / dist;

		float h = dot(vDir, Nvs);
		horizon = max(horizon, h);

		if (horizon > 0.99f)
			break;
	}

	float occ = saturate((horizon - GTAO_MIN_HORIZON_COS) / max(1.0f - GTAO_MIN_HORIZON_COS, 1e-5f));
	return occ;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 outSize;
	g_OutAO.GetDimensions(outSize.x, outSize.y);

	if (DTid.x >= outSize.x || DTid.y >= outSize.y)
		return;

    // half-res pixel center -> uv
	float2 uv = (float2(DTid.xy) + 0.5f) / float2(outSize);

	float depth01 = g_GBufferDepth.SampleLevel(g_PointClampSampler, uv, 0);
	if (depth01 >= 0.999999f)
	{
		g_OutAO[DTid.xy] = 1.0f;
		return;
	}

	float3 Pvs = ReconstructViewPos(uv, depth01);

	float3 Nws = UnpackNormal01(g_GBufferNormal.SampleLevel(g_PointClampSampler, uv, 0).xyz);
	float3 Nvs = WorldNormalToView(Nws);

	float3 Tvs, Bvs;
	BuildOrthoBasis(Nvs, Tvs, Bvs);

    // blue-noise (use full-res-ish pixel id: half pixel * 2)
	uint2 pixFull = DTid.xy * 2u;
	float noise = GetBlueNoiseDither(pixFull);

	float ang = noise * 6.28318530718f;
	float s, c;
	sincos(ang, s, c);
	float2 rot = float2(c, s);

	float occSum = 0.0f;

    [unroll]
	for (int d = 0; d < GTAO_NUM_DIRECTIONS; ++d)
	{
		float a = (float(d) * (1.0f / float(GTAO_NUM_DIRECTIONS))) * 6.28318530718f;
		float sd, cd;
		sincos(a, sd, cd);

		float2 dir2 = Rotate2D(float2(cd, sd), rot);
		float3 dirVS = normalize(Tvs * dir2.x + Bvs * dir2.y);

		float dirNoise = frac(noise + float(d) * 0.61803398875f);

		occSum += EvaluateHorizonOneDirVS(uv, depth01, Pvs, Nvs, dirVS, dirNoise);
	}

	float occAvg = occSum * (1.0f / float(GTAO_NUM_DIRECTIONS));

	float ao = 1.0f - saturate(pow(occAvg * GTAO_INTENSITY, GTAO_POWER));
	g_OutAO[DTid.xy] = ao;
}