#include "Common.hlsli"

#ifndef SHZ_SHADOWS_HLSLI
#define SHZ_SHADOWS_HLSLI

// -----------------------------------------------------------------------------
// NDC -> UV / Depth
// -----------------------------------------------------------------------------
static float2 NdcXY_To_UV(float2 ndcXY)
{
	float2 uv = ndcXY * 0.5 + 0.5;
	uv.y = 1.0 - uv.y;
	return uv;
}
static float NdcZ_To_Depth(float ndcZ)
{
	return ndcZ;
}

// u =  0.5*ndc.x + 0.5
// v = -0.5*ndc.y + 0.5
// d =  1.0*ndc.z
static const float3 NDC_TO_UVD = float3(0.5, -0.5, 1.0);

// -----------------------------------------------------------------------------
// ShadowMapAttribs accessors (NO ARRAYS IN CB)
// -----------------------------------------------------------------------------
static CascadeAttribs GetCascadeAttribs_i(int idx)
{
	switch (idx)
	{
		default:
		case 0:
			return g_ShadowAttribs.Cascades0;
		case 1:
			return g_ShadowAttribs.Cascades1;
		case 2:
			return g_ShadowAttribs.Cascades2;
		case 3:
			return g_ShadowAttribs.Cascades3;
		case 4:
			return g_ShadowAttribs.Cascades4;
		case 5:
			return g_ShadowAttribs.Cascades5;
		case 6:
			return g_ShadowAttribs.Cascades6;
		case 7:
			return g_ShadowAttribs.Cascades7;
	}
}

static float GetCascadeEndZ(int idx)
{
	switch (idx)
	{
		default:
		case 0:
			return g_ShadowAttribs.CascadeCamSpaceZEnd0.x;
		case 1:
			return g_ShadowAttribs.CascadeCamSpaceZEnd0.y;
		case 2:
			return g_ShadowAttribs.CascadeCamSpaceZEnd0.z;
		case 3:
			return g_ShadowAttribs.CascadeCamSpaceZEnd0.w;
		case 4:
			return g_ShadowAttribs.CascadeCamSpaceZEnd1.x;
		case 5:
			return g_ShadowAttribs.CascadeCamSpaceZEnd1.y;
		case 6:
			return g_ShadowAttribs.CascadeCamSpaceZEnd1.z;
		case 7:
			return g_ShadowAttribs.CascadeCamSpaceZEnd1.w;
	}
}

// -----------------------------------------------------------------------------
// Distance to cascade margin (Diligent-like)
// posProj.xy in [-1..1], z in [0..1]
// -----------------------------------------------------------------------------
float GetDistanceToCascadeMargin(float3 posInCascadeProjSpace, float4 marginProjSpace)
{
	float4 distToEdges;
	distToEdges.xy = 1.0.xx - marginProjSpace.xy - abs(posInCascadeProjSpace.xy);

	const float ZScale = 2.0 / (1.0 - 0.0);
	distToEdges.z = (posInCascadeProjSpace.z - (0.0 + marginProjSpace.z)) * ZScale;
	distToEdges.w = (1.0 - marginProjSpace.w - posInCascadeProjSpace.z) * ZScale;

	return min(min(distToEdges.x, distToEdges.y), min(distToEdges.z, distToEdges.w));
}

// -----------------------------------------------------------------------------
// Cascade sampling info
// -----------------------------------------------------------------------------
struct CascadeSamplingInfo
{
	int CascadeIdx;
	float2 UV;
	float Depth;

	float3 LightSpaceScale;
	float MinDistToMargin;

	float4x4 WorldToLightView;
};

CascadeSamplingInfo GetCascadeSamplingInfo(float3 worldPos, float cameraViewSpaceZ, int cascadeIdx)
{
	CascadeAttribs C = GetCascadeAttribs_i(cascadeIdx);

	float3 posLS = mul(float4(worldPos, 1.0), C.WorldToLightView).xyz;
	float3 posProj = posLS * C.LightSpaceScale.xyz + C.LightSpaceScaledBias.xyz;

	CascadeSamplingInfo S;
	S.CascadeIdx = cascadeIdx;
	S.UV = NdcXY_To_UV(posProj.xy);
	S.Depth = NdcZ_To_Depth(posProj.z);

	S.LightSpaceScale = C.LightSpaceScale.xyz;
	S.MinDistToMargin = GetDistanceToCascadeMargin(posProj, C.MarginProjSpace);
	S.WorldToLightView = C.WorldToLightView;
	return S;
}

// -----------------------------------------------------------------------------
// Fast cascade selection using camera view-space Z ends (NO ARRAYS)
// -----------------------------------------------------------------------------
int FindCascadeIndex(float cameraViewSpaceZ)
{
	int idx = 0;
	if (GetCascadeEndZ(0) < cameraViewSpaceZ)
		idx++;
	if (GetCascadeEndZ(1) < cameraViewSpaceZ)
		idx++;
	if (GetCascadeEndZ(2) < cameraViewSpaceZ)
		idx++;
	if (GetCascadeEndZ(3) < cameraViewSpaceZ)
		idx++;
	if (GetCascadeEndZ(4) < cameraViewSpaceZ)
		idx++;
	if (GetCascadeEndZ(5) < cameraViewSpaceZ)
		idx++;
	if (GetCascadeEndZ(6) < cameraViewSpaceZ)
		idx++;
	if (GetCascadeEndZ(7) < cameraViewSpaceZ)
		idx++;
	return idx;
}

// -----------------------------------------------------------------------------
// Next cascade blend amount
// -----------------------------------------------------------------------------
float GetNextCascadeBlendAmount(
	float cameraViewSpaceZ,
	CascadeSamplingInfo Cur,
	CascadeSamplingInfo Next)
{
	CascadeAttribs CurC = GetCascadeAttribs_i(Cur.CascadeIdx);
	float4 startEndZ = CurC.StartEndZ;

	float distToTransitionEdge =
		(startEndZ.y - cameraViewSpaceZ) / max(startEndZ.y - startEndZ.x, 1e-6);

	distToTransitionEdge = max(distToTransitionEdge, Cur.MinDistToMargin);

	float a = saturate(1.0 - distToTransitionEdge / max(g_ShadowAttribs.CascadeTransitionRegion, 1e-6));
	a *= saturate(Next.MinDistToMargin / 0.01);
	return a;
}

// -----------------------------------------------------------------------------
// Receiver-plane depth bias
// -----------------------------------------------------------------------------
float2 ComputeReceiverPlaneDepthBias(float3 ShadowUVDepthDX, float3 ShadowUVDepthDY)
{
	float2 biasUV;
	biasUV.x = ShadowUVDepthDY.y * ShadowUVDepthDX.z - ShadowUVDepthDX.y * ShadowUVDepthDY.z;
	biasUV.y = -ShadowUVDepthDY.x * ShadowUVDepthDX.z + ShadowUVDepthDX.x * ShadowUVDepthDY.z;

	float Det = (ShadowUVDepthDX.x * ShadowUVDepthDY.y) - (ShadowUVDepthDX.y * ShadowUVDepthDY.x);
	biasUV /= sign(Det) * max(abs(Det), 1e-10);
	return biasUV;
}

// -----------------------------------------------------------------------------
// Fixed 5x5 PCF
// -----------------------------------------------------------------------------
float FilterShadowFixedPCF_3x3(float2 uv, int cascadeIdx, float depth, float2 depthSlopeScaledBiasUV)
{
	float2 texel = g_ShadowAttribs.ShadowMapDim.zw;

	float sum = 0.0;
	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float2 duv = float2(x, y) * texel + depthSlopeScaledBiasUV;
			sum += g_ShadowMapArray.SampleCmpLevelZero(
				g_ShadowCmpSampler,
				float3(uv + duv, cascadeIdx),
				depth);
		}
	}
	return sum / 9.0;
}

// -----------------------------------------------------------------------------
// Filter a single cascade (per-cascade WorldToLightView for ddx/ddy too)
// -----------------------------------------------------------------------------
float FilterShadowCascade(
	float3 worldPos,
	float3 ddxWorldPos,
	float3 ddyWorldPos,
	CascadeSamplingInfo S)
{
	CascadeAttribs C = GetCascadeAttribs_i(S.CascadeIdx);

	float3 posLS = mul(float4(worldPos, 1.0), C.WorldToLightView).xyz;
	float3 ddxLS = mul(float4(ddxWorldPos, 0.0), C.WorldToLightView).xyz;
	float3 ddyLS = mul(float4(ddyWorldPos, 0.0), C.WorldToLightView).xyz;

	float3 ddxUVD = ddxLS * C.LightSpaceScale.xyz * NDC_TO_UVD;
	float3 ddyUVD = ddyLS * C.LightSpaceScale.xyz * NDC_TO_UVD;

	float2 slopeBias = ComputeReceiverPlaneDepthBias(ddxUVD, ddyUVD);

	float2 slopeClamp =
		abs((C.LightSpaceScale.z * NDC_TO_UVD.z) /
			max(C.LightSpaceScale.xy * NDC_TO_UVD.xy, 1e-6.xx)) *
		g_ShadowAttribs.ReceiverPlaneDepthBiasClamp;

	slopeBias = clamp(slopeBias, -slopeClamp, slopeClamp);

	slopeBias *= g_ShadowAttribs.ShadowMapDim.zw;

	float fractionalError = dot(abs(slopeBias), 1.0.xx) + g_ShadowAttribs.FixedDepthBias;
	float depth = S.Depth - fractionalError;

	return FilterShadowFixedPCF_3x3(S.UV, S.CascadeIdx, depth, slopeBias);
}

// -----------------------------------------------------------------------------
// Public output
// -----------------------------------------------------------------------------
struct FilteredShadow
{
	float LightAmount;
	int CascadeIdx;
	float NextCascadeBlend;
};

// -----------------------------------------------------------------------------
// Main entry
// cameraViewSpaceZ: main camera view-space Z (+forward)
// -----------------------------------------------------------------------------
FilteredShadow FilterShadowMapCSM(
	float3 worldPos,
	float3 ddxWorldPos,
	float3 ddyWorldPos,
	float cameraViewSpaceZ,
	bool filterAcrossCascades)
{
	FilteredShadow Out;
	Out.CascadeIdx = FindCascadeIndex(cameraViewSpaceZ);
	Out.NextCascadeBlend = 0.0;
	Out.LightAmount = 1.0;

	if (Out.CascadeIdx >= g_ShadowAttribs.NumCascades)
		return Out;

	CascadeSamplingInfo S = GetCascadeSamplingInfo(worldPos, cameraViewSpaceZ, Out.CascadeIdx);
	Out.LightAmount = FilterShadowCascade(worldPos, ddxWorldPos, ddyWorldPos, S);

	if (filterAcrossCascades && (Out.CascadeIdx + 1 < g_ShadowAttribs.NumCascades))
	{
		CascadeSamplingInfo N = GetCascadeSamplingInfo(worldPos, cameraViewSpaceZ, Out.CascadeIdx + 1);
		float a = GetNextCascadeBlendAmount(cameraViewSpaceZ, S, N);
		Out.NextCascadeBlend = a;

		if (a > 0.0)
		{
			float nShadow = FilterShadowCascade(worldPos, ddxWorldPos, ddyWorldPos, N);
			Out.LightAmount = lerp(Out.LightAmount, nShadow, a);
		}
	}

	return Out;
}

#endif // SHZ_SHADOWS_HLSLI
