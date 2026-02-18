#include "Common.hlsli"

#ifndef SHZ_SHADOWS_HLSLI
#define SHZ_SHADOWS_HLSLI

// ============================================================================
// NOTE (CB ARRAY 제거 버전)
// - HLSL_Structures.hlsli에서:
//   * ShadowMapAttribs.CascadeCamSpaceZEnd[2]  -> CascadeCamSpaceZEnd0 / 1
//   * ShadowMapAttribs.Cascades[8]            -> Cascades0..7
// 로 바뀐 것에 맞춰 이 파일도 전부 교체.
// ============================================================================

// -----------------------------------------------------------------------------
// NDC -> UV / Depth (통일)
// -----------------------------------------------------------------------------
static float2 NdcXY_To_UV(float2 ndcXY)
{
	float2 uv = ndcXY * 0.5 + 0.5;
	uv.y = 1.0 - uv.y; // D3D UV convention (top-left)
	return uv;
}

static float NdcZ_To_Depth(float ndcZ)
{
	// D3D depth: [0..1]
	return ndcZ;
}

// Unified conversion vector used for ddx/ddy mapping:
//   u =  0.5*ndc.x + 0.5
//   v = -0.5*ndc.y + 0.5
//   d =  1.0*ndc.z
static const float3 NDC_TO_UVD = float3(0.5, -0.5, 1.0);

// -----------------------------------------------------------------------------
// ShadowMapAttribs accessors (NO ARRAYS IN CB)
// -----------------------------------------------------------------------------
static CascadeAttribs GetCascadeAttribs(int idx)
{
	// NOTE: idx is expected 0..7
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
	// CascadeCamSpaceZEnd0: x,y,z,w => 0..3
	// CascadeCamSpaceZEnd1: x,y,z,w => 4..7
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
// posInCascadeProjSpace.xy in [-1..1], z in [0..1]
// -----------------------------------------------------------------------------
float GetDistanceToCascadeMargin(float3 posInCascadeProjSpace, float4 marginProjSpace)
{
	float4 distToEdges;
	distToEdges.xy = 1.0.xx - marginProjSpace.xy - abs(posInCascadeProjSpace.xy);

	// z range is [0..1]
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
};

CascadeSamplingInfo GetCascadeSamplingInfo(float3 posInLightViewSpace, int cascadeIdx)
{
	CascadeAttribs C = GetCascadeAttribs(cascadeIdx);

	const float3 lightSpaceScale = C.LightSpaceScale.xyz;
	const float3 posInCascadeProjSpace = posInLightViewSpace * lightSpaceScale + C.LightSpaceScaledBias.xyz;

	CascadeSamplingInfo S;
	S.CascadeIdx = cascadeIdx;

	S.UV = NdcXY_To_UV(posInCascadeProjSpace.xy);
	S.Depth = NdcZ_To_Depth(posInCascadeProjSpace.z);

	S.LightSpaceScale = lightSpaceScale;
	S.MinDistToMargin = GetDistanceToCascadeMargin(posInCascadeProjSpace, C.MarginProjSpace);
	return S;
}

// -----------------------------------------------------------------------------
// Fast cascade selection using camera view-space Z ends (NO ARRAYS)
// -----------------------------------------------------------------------------
CascadeSamplingInfo FindCascade(float3 posInLightViewSpace, float cameraViewSpaceZ)
{
	CascadeSamplingInfo S;
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

	if (idx < g_ShadowAttribs.NumCascades)
		S = GetCascadeSamplingInfo(posInLightViewSpace, idx);

	S.CascadeIdx = idx;
	return S;
}


// -----------------------------------------------------------------------------
// Next cascade blend amount (transition region + margin safety)
// -----------------------------------------------------------------------------
float GetNextCascadeBlendAmount(
	float cameraViewSpaceZ,
	CascadeSamplingInfo Cur,
	CascadeSamplingInfo Next)
{
	CascadeAttribs CurC = GetCascadeAttribs(Cur.CascadeIdx);
	float4 startEndZ = CurC.StartEndZ;

	float distToTransitionEdge =
		(startEndZ.y - cameraViewSpaceZ) / max(startEndZ.y - startEndZ.x, 1e-6);

	// include margin so we don't blend from/to outside area
	distToTransitionEdge = max(distToTransitionEdge, Cur.MinDistToMargin);

	float a = saturate(1.0 - distToTransitionEdge / max(g_ShadowAttribs.CascadeTransitionRegion, 1e-6));

	// next margin safety
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
// Fixed 5x5 PCF (comparison sampler)
// g_ShadowAttribs.ShadowMapDim.zw must be texel size (1/width, 1/height)
// -----------------------------------------------------------------------------
float FilterShadowFixedPCF_5x5(
	float2 uv,
	int cascadeIdx,
	float depth,
	float2 depthSlopeScaledBiasUV)
{
	float2 texel = g_ShadowAttribs.ShadowMapDim.zw;

	float sum = 0.0;
	[unroll]
	for (int y = -2; y <= 2; ++y)
	{
		[unroll]
		for (int x = -2; x <= 2; ++x)
		{
			float2 duv = float2(x, y) * texel + depthSlopeScaledBiasUV;
			sum += g_ShadowMapArray.SampleCmpLevelZero(
				g_ShadowCmpSampler,
				float3(uv + duv, cascadeIdx),
				depth);
		}
	}
	return sum / 25.0;
}

// -----------------------------------------------------------------------------
// Filter a single cascade
// -----------------------------------------------------------------------------
float FilterShadowCascade(
	float3 ddxPosInLightView,
	float3 ddyPosInLightView,
	CascadeSamplingInfo S)
{
	// ddx/ddy of (U,V,Depth) in texture space.
	// u =  0.5*ndc.x+0.5, v = -0.5*ndc.y+0.5, depth = ndc.z
	float3 ddxUVD = ddxPosInLightView * S.LightSpaceScale * NDC_TO_UVD;
	float3 ddyUVD = ddyPosInLightView * S.LightSpaceScale * NDC_TO_UVD;

	float2 slopeBias = ComputeReceiverPlaneDepthBias(ddxUVD, ddyUVD);

	// clamp in UV space based on cascade scale (same intent as Diligent)
	float2 slopeClamp =
		abs((S.LightSpaceScale.z * NDC_TO_UVD.z) /
			max(S.LightSpaceScale.xy * NDC_TO_UVD.xy, 1e-6.xx)) *
		g_ShadowAttribs.ReceiverPlaneDepthBiasClamp;

	slopeBias = clamp(slopeBias, -slopeClamp, slopeClamp);

	// convert slope bias to UV units (texels -> UV)
	slopeBias *= g_ShadowAttribs.ShadowMapDim.zw;

	// depth bias: fractional error from slope + fixed depth bias
	float fractionalError = dot(abs(slopeBias), 1.0.xx) + g_ShadowAttribs.FixedDepthBias;
	float depth = S.Depth - fractionalError;

	return FilterShadowFixedPCF_5x5(
		S.UV,
		S.CascadeIdx,
		depth,
		slopeBias);
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
// Main entry: filter CSM shadow map array
// -----------------------------------------------------------------------------
FilteredShadow FilterShadowMapCSM(
	float3 worldPos,
	float3 ddxWorldPos,
	float3 ddyWorldPos,
	float cameraViewSpaceZ,
	bool filterAcrossCascades)
{
	// World -> Light view space
	float3 posLS = mul(float4(worldPos, 1.0), g_ShadowAttribs.WorldToLightView).xyz;
	float3 ddxLS = mul(float4(ddxWorldPos, 0.0), g_ShadowAttribs.WorldToLightView).xyz;
	float3 ddyLS = mul(float4(ddyWorldPos, 0.0), g_ShadowAttribs.WorldToLightView).xyz;

	CascadeSamplingInfo S = FindCascade(posLS, cameraViewSpaceZ);

	FilteredShadow Out;
	Out.CascadeIdx = S.CascadeIdx;
	Out.NextCascadeBlend = 0.0;
	Out.LightAmount = 1.0;

	if (S.CascadeIdx >= g_ShadowAttribs.NumCascades)
		return Out;

	Out.LightAmount = FilterShadowCascade(ddxLS, ddyLS, S);

	if (filterAcrossCascades && (S.CascadeIdx + 1 < g_ShadowAttribs.NumCascades))
	{
		CascadeSamplingInfo N = GetCascadeSamplingInfo(posLS, S.CascadeIdx + 1);
		float a = GetNextCascadeBlendAmount(cameraViewSpaceZ, S, N);
		Out.NextCascadeBlend = a;

		if (a > 0.0)
		{
			float nShadow = FilterShadowCascade(ddxLS, ddyLS, N);
			Out.LightAmount = lerp(Out.LightAmount, nShadow, a);
		}
	}

	return Out;
}

#endif // SHZ_SHADOWS_HLSLI
