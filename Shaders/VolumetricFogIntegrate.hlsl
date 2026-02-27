#include "VolumetricFogCommon.hlsli"

Texture3D<float4> g_ScatterVolume; // rgb=scatter, a=sigmaT
RWTexture3D<float4> g_OutIntegrated; // rgb=L, a=T

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint w, h, z;
	g_OutIntegrated.GetDimensions(w, h, z);

	if (tid.x >= w || tid.y >= h)
		return;

	float2 uv = (float2(tid.x, tid.y) + 0.5) / float2(w, h);

	// Convert viewZ step to ray distance step
	float3 rayDirWS = ReconstructWorldRayDir(uv);
	float3 rayDirVS = mul(float4(rayDirWS, 0.0), g_FrameCB.View).xyz;
	float invCos = 1.0 / max(rayDirVS.z, 1e-4);

	float3 Lacc = 0.0;
	float Tacc = 1.0;

	float maxDist = g_FogCB.MaxDistance; // 0 means no clamp

	for (uint zi = 0; zi < z; ++zi)
	{
		float t0 = (float) zi / (float) z;
		float t1 = (float) (zi + 1) / (float) z;

		float viewZ0 = ViewZFromT(t0);
		float viewZ1 = ViewZFromT(t1);

		// Clamp integration distance
		if (maxDist > 0.0)
		{
			if (viewZ0 >= maxDist)
			{
				// Beyond max distance: keep writing the same accumulated value
				g_OutIntegrated[uint3(tid.x, tid.y, zi)] = float4(Lacc, Tacc);
				continue;
			}
			viewZ1 = min(viewZ1, maxDist);
		}

		float ds = (viewZ1 - viewZ0) * invCos;
		ds = max(ds, 0.0);

		float tCenter = ((float) zi + 0.5) / (float) z;
		float4 sc = g_ScatterVolume.SampleLevel(g_LinearClampSampler, float3(uv, tCenter), 0);

		float3 scatter = sc.rgb;
		float sigmaT = max(sc.a, 0.0);

		// Beer-Lambert
		float stepT = (sigmaT > 0.0) ? exp(-sigmaT * ds) : 1.0;

		// Accumulate
		Lacc += Tacc * scatter * ds;
		Tacc *= stepT;

		g_OutIntegrated[uint3(tid.x, tid.y, zi)] = float4(Lacc, Tacc);
	}
}