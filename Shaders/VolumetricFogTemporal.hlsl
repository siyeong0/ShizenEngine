#include "VolumetricFogCommon.hlsli"

Texture3D<float4> g_Integrated; // curr (rgb=L, a=T)
Texture3D<float4> g_HistoryPrev; // prev history (rgb=L, a=T)

RWTexture3D<float4> g_OutFinal; // final (rgb=L, a=T)
RWTexture3D<float4> g_HistoryCurr; // store history

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
float3 FroxelUVW(uint3 tid, uint w, uint h, uint z)
{
	float2 uv = (float2(tid.x, tid.y) + 0.5) / float2(w, h);
	float t = ((float) tid.z + 0.5) / (float) z;
	return float3(uv, t);
}

float3 WorldPosFromFroxel(float2 uv, float tCenter)
{
	float viewZ = ViewZFromT(tCenter);
	return ReconstructWorldPosFromViewZ(uv, viewZ);
}

float ComputeViewZFromWorld(float3 worldPos, float4x4 view)
{
	float3 vp = mul(float4(worldPos, 1.0), view).xyz;
	return max(vp.z, 1e-4);
}

float4 HistoryRejectFilter(float4 curr, float4 prev, float thr)
{
	if (abs(curr.a - prev.a) > thr)
		return curr;

	return prev;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint w, h, z;
	g_OutFinal.GetDimensions(w, h, z);

	if (tid.x >= w || tid.y >= h || tid.z >= z)
		return;

	const float3 uvw = FroxelUVW(tid, w, h, z);

	// Current sample
	float4 curr = g_Integrated.SampleLevel(g_LinearClampSampler, uvw, 0);

	// ------------------------------------------------------------
	// Reproject: current froxel center -> world -> prev UV + prev T
	// ------------------------------------------------------------
	const float2 uvCurr = uvw.xy;
	const float tCurr = uvw.z;

	float3 worldPos = WorldPosFromFroxel(uvCurr, tCurr);

	// Prev clip position
	float4 prevClip = mul(float4(worldPos, 1.0), g_FrameCB.PrevViewProj);

	// Behind camera or invalid
	if (prevClip.w <= 1e-6)
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	float2 prevNDC = prevClip.xy / prevClip.w;
	float2 prevUV = prevNDC * 0.5 + 0.5;

	// Screen clamp/reject
	// - clamp로 가면 테두리 smear가 생기고,
	// - reject로 가면 빠른 이동에서 약간 noise가 생김.
	// 보통 reject가 더 안전.
	if (any(prevUV < 0.0) || any(prevUV > 1.0))
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	// Prev viewZ -> prevT
	float prevViewZ = ComputeViewZFromWorld(worldPos, g_FrameCB.PrevView);

	// MaxDistance 적용(Scatter/Integrate에서 이미 했더라도 temporal에서 한 번 더 방어)
	if (g_FogCB.MaxDistance > 0.0 && prevViewZ >= g_FogCB.MaxDistance)
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	float prevT = TFromViewZ(prevViewZ);

	// Volume bounds
	// (t는 0..1이지만, log mapping 때문에 near 근처에서 민감할 수 있음)
	if (prevT < 0.0 || prevT > 1.0)
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	// Sample history with reprojection coords
	float4 prev = g_HistoryPrev.SampleLevel(g_LinearClampSampler, float3(prevUV, prevT), 0);

	// History reject
	prev = HistoryRejectFilter(curr, prev, g_FogCB.HistoryRejectThreshold);

	// EMA blend
	float alpha = saturate(g_FogCB.TemporalAlpha);
	float4 outv = lerp(curr, prev, alpha);

	g_OutFinal[tid] = outv;
	g_HistoryCurr[tid] = outv;
}