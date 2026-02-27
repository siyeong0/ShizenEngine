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

	// -------------------------------------------------------------------------
	// 1) Base UV at froxel center
	// -------------------------------------------------------------------------
	float2 uv = (float2(tid.x, tid.y) + 0.5) / float2(w, h);

	// -------------------------------------------------------------------------
	// 2) Frame jitter (Halton) in UV space
	//    - This is the big stabilizer: "frame-regular" jitter like TAA
	//    - Strength unit: pixels
	// -------------------------------------------------------------------------
	if (g_FogCB.JitterStrength > 0.0)
	{
		float2 j01 = GetHaltonJitter01(g_FrameCB.FrameIndex);
		float2 jUV = (j01 * g_FogCB.JitterStrength) / float2(w, h);
		uv += jUV;
		uv = saturate(uv);
	}

	// -------------------------------------------------------------------------
	// 3) Slice center in t-space
	// -------------------------------------------------------------------------
	float tCenter = ((float) tid.z + 0.5) / (float) z;

	// -------------------------------------------------------------------------
	// 4) Per-froxel jitter on t (spatial noise)
	//    - Reduces banding within the volume grid
	//    - Keep it smaller than 1 slice
	// -------------------------------------------------------------------------
	if (g_FogCB.JitterStrength > 0.0)
	{
		float n = InterleavedGradientNoise(float2(tid.xy) + 0.5, g_FrameCB.FrameIndex); // [0..1)
		float jt = (n - 0.5) * (g_FogCB.JitterStrength / (float) z);
		tCenter = saturate(tCenter + jt);
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
	float sigmaT = density * g_FogCB.ExtinctionScale;
	float sigmaS = sigmaT * saturate(g_FogCB.Albedo);

	// Directional light (FrameCB convention)
	float3 L = normalize(-g_FrameCB.LightDirWS);

	// View direction at cell (towards camera)
	float3 V = normalize(g_FrameCB.CameraPosition - worldPos);

	// Phase
	float cosTheta = dot(L, -V);
	float phase = PhaseHG(cosTheta, saturate(g_FogCB.AnisotropyG));

	// Shadow visibility
	float3 ddxWP = 0;
	float3 ddyWP = 0;
	float3 viewPos = mul(float4(worldPos, 1.0), g_FrameCB.View).xyz;

	FilteredShadow sh = FilterShadowMapCSM(worldPos, ddxWP, ddyWP, viewPos.z, true);
	float vis = sh.LightAmount;

	// Light radiance (if LightColor is sRGB packed, squaring is one hacky way)
	float3 lightRadiance = g_FrameCB.LightColor * g_FrameCB.LightColor * g_FrameCB.LightIntensity;

	// Scatter per unit length (radiance density)
	float3 scatter = lightRadiance * vis * (sigmaS * phase);

	// Artistic controls
	scatter *= g_FogCB.FogColor;
	scatter *= max(g_FogCB.PhaseBoost, 0.0);

	g_OutScatter[tid] = float4(scatter, sigmaT);
}