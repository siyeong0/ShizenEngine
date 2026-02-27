#include "VolumetricFogCommon.hlsli"

Texture3D<float4> g_Integrated; // curr (rgb=L, a=T or sigma-integrated output)
Texture3D<float4> g_HistoryPrev; // prev history (rgb=L, a=...)

RWTexture3D<float4> g_OutFinal; // final (rgb=L, a=...)
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

bool OutOf01(float2 uv)
{
	return any(uv < 0.0) || any(uv > 1.0);
}

// Depth/Extinction-based reject (네가 prev.a에 뭘 저장하는지에 따라 thr 튜닝)
float4 HistoryRejectFilter(float4 curr, float4 prev, float thrA)
{
	if (abs(curr.a - prev.a) > thrA)
		return curr;
	return prev;
}

// Velocity-adaptive alpha: 움직임 클수록 history 덜 믿기
float ComputeAdaptiveAlpha(float baseAlpha, float2 uvCurr, float2 uvPrev, float velScale)
{
	float2 v = (uvCurr - uvPrev);
	float vel = length(v); // in UV units
	float k = exp(-vel * velScale); // 0..1
	return saturate(baseAlpha * k);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint w, h, z;
	g_OutFinal.GetDimensions(w, h, z);

	if (tid.x >= w || tid.y >= h || tid.z >= z)
		return;

	float3 uvw = FroxelUVW(tid, w, h, z);

    // Current sample
	float4 curr = g_Integrated.SampleLevel(g_LinearClampSampler, uvw, 0);

    // -------------------------------------------------------------------------
    // Reproject: current froxel center -> world -> prev UV + prev T
    // -------------------------------------------------------------------------
	float2 uvCurr = uvw.xy;
	float tCurr = uvw.z;

	float3 worldPos = WorldPosFromFroxel(uvCurr, tCurr);

	float4 prevClip = mul(float4(worldPos, 1.0), g_FrameCB.PrevViewProj);

	if (prevClip.w <= 1e-6)
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	float2 prevNDC = prevClip.xy / prevClip.w;
	float2 prevUV = prevNDC * 0.5 + 0.5;

	if (OutOf01(prevUV))
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	float prevViewZ = ComputeViewZFromWorld(worldPos, g_FrameCB.PrevView);

	if (g_FogCB.MaxDistance > 0.0 && prevViewZ >= g_FogCB.MaxDistance)
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	float prevT = TFromViewZ(prevViewZ);
	if (prevT < 0.0 || prevT > 1.0)
	{
		g_OutFinal[tid] = curr;
		g_HistoryCurr[tid] = curr;
		return;
	}

	float4 prev = g_HistoryPrev.SampleLevel(g_LinearClampSampler, float3(prevUV, prevT), 0);

    // -------------------------------------------------------------------------
    // Reject (depth/σT)
    // -------------------------------------------------------------------------
	prev = HistoryRejectFilter(curr, prev, g_FogCB.HistoryRejectThreshold);

    // -------------------------------------------------------------------------
    // Neighborhood clamp (variance clip)
    // - clamp history inside current neighborhood range to suppress shimmer/smear
    // -------------------------------------------------------------------------
	float2 invWH = 1.0 / float2(w, h);
	float invZ = 1.0 / (float) z;

	float3 nMinRGB, nMaxRGB;
	float nMinA, nMaxA;
	GatherNeighborhoodMinMax_3x3x1(
        g_Integrated, g_LinearClampSampler, uvw, invWH, invZ,
        nMinRGB, nMaxRGB, nMinA, nMaxA);

    // Expand clamp box a bit
	prev = NeighborhoodClampHistory(prev, nMinRGB, nMaxRGB, nMinA, nMaxA, g_FogCB.HistoryClampExpand);

    // -------------------------------------------------------------------------
    // Velocity-adaptive EMA alpha
    // - fast camera motion -> reduce history weight
    // -------------------------------------------------------------------------
	float baseAlpha = saturate(g_FogCB.TemporalAlpha);

    // velScale: 튜닝 포인트 (UV 기준). 보통 200~800 사이가 무난.
	float alpha = ComputeAdaptiveAlpha(baseAlpha, uvCurr, prevUV, g_FogCB.TemporalVelocityScale);

	float4 outv = lerp(curr, prev, alpha);

	g_OutFinal[tid] = outv;
	g_HistoryCurr[tid] = outv;
}