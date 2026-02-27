#include "VolumetricFogCommon.hlsli"
#include "Shadow.hlsli"

// Output: rgb=scatter radiance (per-step), a=extinction coefficient (sigmaT)
RWTexture3D<float4> g_OutScatter;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint w, h, z;
	g_OutScatter.GetDimensions(w, h, z);

	if (tid.x >= w || tid.y >= h || tid.z >= z)
		return;

	float2 uv = (float2(tid.x, tid.y) + 0.5) / float2(w, h);

	// Slice center in t-space
	float tCenter = ((float) tid.z + 0.5) / (float) z;

	// Per-froxel jitter on t to reduce banding (temporal helps)
	if (g_FogCB.JitterStrength > 0.0)
	{
		uint seed = tid.x * 73856093u ^ tid.y * 19349663u ^ tid.z * 83492791u ^ (uint) g_FrameCB.FrameIndex;
		float j = HashToUNorm01(seed) - 0.5;
		tCenter += j * (g_FogCB.JitterStrength / (float) z);
		tCenter = saturate(tCenter);
	}

	float viewZ = ViewZFromT(tCenter);

	// MaxDistance clamp (meters, in viewZ units)
	if (g_FogCB.MaxDistance > 0.0 && viewZ >= g_FogCB.MaxDistance)
	{
		g_OutScatter[tid] = float4(0, 0, 0, 0);
		return;
	}

	float3 worldPos = ReconstructWorldPosFromViewZ(uv, viewZ);

	// Density
	float density = ComputeFogDensity(worldPos);

	// Extinction/scattering coefficients
	// sigmaT = density * ExtinctionScale
	float sigmaT = density * g_FogCB.ExtinctionScale;
	// sigmaS = sigmaT * Albedo  (stable, physically interpretable)
	float sigmaS = sigmaT * saturate(g_FogCB.Albedo);

	// Directional light (your FrameCB convention)
	float3 L = normalize(-g_FrameCB.LightDirWS);

	// View direction at cell (towards camera)
	float3 V = normalize(g_FrameCB.CameraPosition - worldPos);

	// Phase
	float cosTheta = dot(L, -V);
	float phase = PhaseHG(cosTheta, saturate(g_FogCB.AnisotropyG));

	// Shadow visibility (same as Lighting.psh)
	float3 ddxWP = 0;
	float3 ddyWP = 0;
	float3 viewPos = mul(float4(worldPos, 1.0), g_FrameCB.View).xyz;

	FilteredShadow sh = FilterShadowMapCSM(worldPos, ddxWP, ddyWP, viewPos.z, true);
	float vis = sh.LightAmount;

	// Light radiance (주의: 너 Lighting.psh에서 LightColor^2 쓰는 이유가 sRGB->linear이면 OK)
	float3 lightRadiance = g_FrameCB.LightColor * g_FrameCB.LightColor * g_FrameCB.LightIntensity;

	// Scatter per unit length (radiance density)
	float3 scatter = lightRadiance * vis * (sigmaS * phase);

	// Artistic controls
	scatter *= g_FogCB.FogColor;
	scatter *= max(g_FogCB.PhaseBoost, 0.0);

	g_OutScatter[tid] = float4(scatter, sigmaT);
}