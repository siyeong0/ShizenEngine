#ifndef GRASS_COMMON_HLSLI
#define GRASS_COMMON_HLSLI

#include "Common.hlsli"
#include "TerrainCommon.hlsli"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const float GRASS_PI = 3.14159265f;
static const float GRASS_TWO_PI = 6.28318530718f;
static const float GRASS_EPS = 1e-8f;

static const float DEFAULT_MAX_PITCH_RAD = 0.55f; // ~31.5 deg

// ---------------------------------------------------------------------------
// Constants buffers
// ---------------------------------------------------------------------------
cbuffer GRASS_RENDER_CONSTANTS
{
	GrassRenderConstants g_GrassCB;
};

// ---------------------------------------------------------------------------
// Math utilities
// ---------------------------------------------------------------------------

// Rotate vector around Y axis by yaw (radians)
float3 ApplyYaw(float3 v, float yaw)
{
	float s = sin(yaw);
	float c = cos(yaw);
	return float3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
}

// Rodrigues rotation formula: rotate v around axisUnit by angle (radians)
float3 RotateAroundAxis(float3 v, float3 axisUnit, float angle)
{
	float s = sin(angle);
	float c = cos(angle);
	return v * c + cross(axisUnit, v) * s + axisUnit * dot(axisUnit, v) * (1.0f - c);
}

// ---------------------------------------------------------------------------
// Packing helpers
// ---------------------------------------------------------------------------

// Pack two UNORM16 into one uint: low16 = A, high16 = B
uint PackUNorm2x16(float a01, float b01)
{
	uint A = (uint) round(saturate(a01) * 65535.0f);
	uint B = (uint) round(saturate(b01) * 65535.0f);
	return (B << 16) | (A & 0xFFFFu);
}

// Pack yaw [0..2pi) to UNORM16
uint PackYaw16(float yawRad)
{
	float y01 = frac(yawRad * (1.0f / GRASS_TWO_PI));
	return (uint) round(saturate(y01) * 65535.0f);
}

// Pack pitch in radians into UNORM16 using symmetric range [-maxPitch..+maxPitch].
uint PackPitch16(float pitchRad, float maxPitchRad)
{
	float m = max(maxPitchRad, 1e-6f);
	float p01 = (pitchRad / m) * 0.5f + 0.5f; // [-m..m] -> [0..1]
	return (uint) round(saturate(p01) * 65535.0f);
}

// Pack [0..1] to UNORM8
uint PackUNorm8(float v01)
{
	return (uint) round(saturate(v01) * 255.0f);
}

// ---------------------------------------------------------------------------
// Decode helpers
// ---------------------------------------------------------------------------
float DecodeUNorm16(uint u16)
{
	return (float) (u16 & 0xFFFFu) * (1.0f / 65535.0f);
}

float DecodeUNorm8(uint u8)
{
	return (float) (u8 & 0xFFu) * (1.0f / 255.0f);
}

// Decode yaw from UNORM16 -> radians [0..2pi)
float DecodeYaw16(uint yaw16)
{
	float yaw01 = DecodeUNorm16(yaw16);
	return yaw01 * GRASS_TWO_PI;
}

// Decode pitch from UNORM16 into radians in range [-maxPitch..+maxPitch].
float DecodePitch16(uint pitch16, float maxPitchRad)
{
	float p01 = DecodeUNorm16(pitch16);
	float pSigned = p01 * 2.0f - 1.0f; // [-1..1]
	return pSigned * max(maxPitchRad, 1e-6f);
}

// ---------------------------------------------------------------------------
// Make helpers (CS-friendly)
// ---------------------------------------------------------------------------

// LOD0: Mesh
GrassMeshInstance MakeGrassMeshInstance(
	float3 posWS,
	float scale,
	float yawRad,
	float pitchRad,
	float bend01,
	float press01,
	uint variantId,
	uint seed8)
{
	GrassMeshInstance o;
	o.PosWS = posWS;
	o.Scale = scale;

	uint yaw16 = PackYaw16(yawRad);
	uint pitch16 = PackPitch16(pitchRad, DEFAULT_MAX_PITCH_RAD);
	o.PackedAngles = (pitch16 << 16) | (yaw16 & 0xFFFFu);

	o.PackedParams =
		((PackUNorm8(bend01) & 0xFFu) << 0) |
		((PackUNorm8(press01) & 0xFFu) << 8) |
		((variantId & 0xFFu) << 16) |
		((seed8 & 0xFFu) << 24);

	return o;
}

// LOD1: Cross-plane
GrassCrossPlaneInstance MakeGrassCrossPlaneInstance(
	float3 posWS,
	float scale,
	float yawRad,
	float bend01,
	float press01,
	uint variantId,
	uint seed8,
	uint atlasFrame,
	uint flags)
{
	GrassCrossPlaneInstance o;
	o.PosWS = posWS;
	o.Scale = scale;

	uint yaw16 = PackYaw16(yawRad);
	o.Packed0 =
		((yaw16 & 0xFFFFu) << 0) |
		((variantId & 0xFFu) << 16) |
		((seed8 & 0xFFu) << 24);

	o.Packed1 =
		((PackUNorm8(bend01) & 0xFFu) << 0) |
		((PackUNorm8(press01) & 0xFFu) << 8) |
		((atlasFrame & 0xFFu) << 16) |
		((flags & 0xFFu) << 24);

	return o;
}

// LOD2: Billboard
GrassBillboardInstance MakeGrassBillboardInstance(
	float3 posWS,
	float scale,
	float yawRad,
	uint atlasIndex,
	uint seed8)
{
	GrassBillboardInstance o;
	o.PosWS = posWS;
	o.Scale = scale;

	uint yaw16 = PackYaw16(yawRad);
	o.Packed =
		((yaw16 & 0xFFFFu) << 0) |
		((atlasIndex & 0xFFu) << 16) |
		((seed8 & 0xFFu) << 24);

	return o;
}

// ---------------------------------------------------------------------------
// Decode helpers (VS/PS-friendly)
// ---------------------------------------------------------------------------
void DecodeGrassMeshInstance(
	GrassMeshInstance inst,
	out float3 posWS,
	out float scale,
	out float yawRad,
	out float pitchRad,
	out float bend01,
	out float press01,
	out uint variantId,
	out uint seed8)
{
	posWS = inst.PosWS;
	scale = inst.Scale;

	uint yaw16 = (inst.PackedAngles & 0xFFFFu);
	uint pitch16 = (inst.PackedAngles >> 16);

	yawRad = DecodeYaw16(yaw16);
	pitchRad = DecodePitch16(pitch16, DEFAULT_MAX_PITCH_RAD);

	bend01 = DecodeUNorm8(inst.PackedParams >> 0);
	press01 = DecodeUNorm8(inst.PackedParams >> 8);
	variantId = (inst.PackedParams >> 16) & 0xFFu;
	seed8 = (inst.PackedParams >> 24) & 0xFFu;
}

void DecodeGrassCrossPlaneInstance(
	GrassCrossPlaneInstance inst,
	out float3 posWS,
	out float scale,
	out float yawRad,
	out float bend01,
	out float press01,
	out uint variantId,
	out uint seed8,
	out uint atlasFrame,
	out uint flags)
{
	posWS = inst.PosWS;
	scale = inst.Scale;

	uint yaw16 = (inst.Packed0 & 0xFFFFu);
	yawRad = DecodeYaw16(yaw16);

	variantId = (inst.Packed0 >> 16) & 0xFFu;
	seed8 = (inst.Packed0 >> 24) & 0xFFu;

	bend01 = DecodeUNorm8(inst.Packed1 >> 0);
	press01 = DecodeUNorm8(inst.Packed1 >> 8);
	atlasFrame = (inst.Packed1 >> 16) & 0xFFu;
	flags = (inst.Packed1 >> 24) & 0xFFu;
}

void DecodeGrassBillboardInstance(
	GrassBillboardInstance inst,
	out float3 posWS,
	out float scale,
	out float yawRad,
	out uint atlasIndex,
	out uint seed8)
{
	posWS = inst.PosWS;
	scale = inst.Scale;

	uint yaw16 = (inst.Packed & 0xFFFFu);
	yawRad = DecodeYaw16(yaw16);

	atlasIndex = (inst.Packed >> 16) & 0xFFu;
	seed8 = (inst.Packed >> 24) & 0xFFu;
}

// -----------------------------------------------------------------------------
// Grass Wind helpers
// -----------------------------------------------------------------------------

static float SoftLimitSigned(float x, float limit)
{
	float l = max(limit, 1e-6f);
	return clamp(x, -l, l);
}

// Triangle wave in [-1..1] with (almost) constant speed between ends.
// x: radians-like phase (can be any continuous value).
static float TriangleWaveSigned(float x)
{
	// frac(x / (2*pi))
	// 1/(2*pi) = 0.159154943091895
	float f = frac(x * 0.159154943091895f);

	// triangle: 1 - 4*abs(f - 0.5)  -> [-1..1]
	float tri = 1.0f - 4.0f * abs(f - 0.5f);
	return tri;
}

// -----------------------------------------------------------------------------
// Grass Position Wind (matches UV wind; uses triangle wave to remove end-dwell)
// -----------------------------------------------------------------------------
// Returns WORLD-SPACE displacement (XZ only).
static float3 ApplyGrassWindPosWS(
	float3 posWS,
	float scale,
	float yaw,
	float bend01,
	float pressHard,
	float keepBase,
	float wTip,
	uint seed8)
{
	float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1.0f, 0.0f));
	float3 windDirWS = float3(windDir2.x, 0.0f, windDir2.y);

	static const float WIND_DIR_JITTER = 0.35f;

	float3 windDirJittered = ApplyYaw(windDirWS, (yaw - GRASS_PI) * WIND_DIR_JITTER);
	windDirJittered.y = 0.0f;
	windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

	float2 windDirJitter2 = NormalizeSafe2(windDirJittered.xz, windDir2);

	float phase =
		dot(posWS.xz, windDir2) * g_GrassCB.WindFreq +
		g_FrameCB.CurrTime * g_GrassCB.WindSpeed +
		yaw * 0.37f;

	float gust =
		1.0f +
		g_GrassCB.WindGust *
		sin(g_FrameCB.CurrTime * (g_GrassCB.WindSpeed * 0.63f) + yaw);

	// IMPORTANT: triangle wave => no "pause" at ends
	float windSignal = TriangleWaveSigned(phase);

	float windFade = saturate(g_GrassCB.InteractionWindFade);
	float windKeep = lerp(1.0f, 1.0f - windFade, pressHard);
	windKeep *= keepBase;

	float rawWindAngle = windSignal * gust * bend01 * g_GrassCB.WindStrength;
	rawWindAngle *= windKeep;

	float maxA = max(g_GrassCB.MaxBendAngle, 1e-4f);
	float windAngle = SoftLimitSigned(rawWindAngle, maxA);

	float hLocal = scale;

	float a = windAngle * wTip;
	float travel = sin(a) * hLocal;

	static const float POS_PER_METER = 0.18f;

	float2 along2 = windDirJitter2;
	float2 perp2 = float2(-along2.y, along2.x);

	float flutter = cos(phase * 1.7f + (float(seed8) * 0.11f) + yaw);

	float2 disp2 =
		along2 * (travel * POS_PER_METER) +
		perp2 * (travel * POS_PER_METER) * 0.35f * flutter;

	return float3(disp2.x, 0.0f, disp2.y);
}

// -----------------------------------------------------------------------------
// Grass UV Wind (same triangle wave signal; matched with position)
// -----------------------------------------------------------------------------
static float2 ApplyGrassWindUV(
	float2 inUV,
	float3 posWS,
	float scale,
	float yaw,
	float bend01,
	float pressHard,
	float keepBase,
	float wTip,
	uint seed8)
{
	float2 uv = inUV;

	float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1.0f, 0.0f));
	float3 windDirWS = float3(windDir2.x, 0.0f, windDir2.y);

	static const float WIND_DIR_JITTER = 0.35f;

	float3 windDirJittered = ApplyYaw(windDirWS, (yaw - GRASS_PI) * WIND_DIR_JITTER);
	windDirJittered.y = 0.0f;
	windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

	float2 windDirJitter2 = NormalizeSafe2(windDirJittered.xz, windDir2);

	float phase =
		dot(posWS.xz, windDir2) * g_GrassCB.WindFreq +
		g_FrameCB.CurrTime * g_GrassCB.WindSpeed +
		yaw * 0.37f;

	float gust =
		1.0f +
		g_GrassCB.WindGust *
		sin(g_FrameCB.CurrTime * (g_GrassCB.WindSpeed * 0.63f) + yaw);

	// IMPORTANT: triangle wave => no "pause" at ends
	float windSignal = TriangleWaveSigned(phase);

	float windFade = saturate(g_GrassCB.InteractionWindFade);
	float windKeep = lerp(1.0f, 1.0f - windFade, pressHard);
	windKeep *= keepBase;

	float rawWindAngle = windSignal * gust * bend01 * g_GrassCB.WindStrength;
	rawWindAngle *= windKeep;

	float maxA = max(g_GrassCB.MaxBendAngle, 1e-4f);
	float windAngle = SoftLimitSigned(rawWindAngle, maxA);

	float hLocal = scale;

	float a = windAngle * wTip;
	float travel = sin(a) * hLocal;

	static const float UV_PER_METER = 0.6f;

	uv += windDirJitter2 * (travel * UV_PER_METER);

	float2 perp = float2(-windDirJitter2.y, windDirJitter2.x);
	float flutter = cos(phase * 1.7f + (float(seed8) * 0.11f) + yaw);
	uv += perp * (travel * UV_PER_METER) * 0.35f * flutter;

	return uv;
}

// -----------------------------------------------------------------------------
// Grass shading shared helpers (PS only)
//
// Notes
// - Keep VS/CS-only utilities in GrassCommon.hlsli.
// - Put PS-only shading utilities here to avoid heavy includes in VS/CS.
//
// Style
// - English comments.
// - Always use braces.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// HSV helpers
// -----------------------------------------------------------------------------
static float3 RgbToHsv(float3 c)
{
	float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);

	float4 p;
	if (c.g < c.b)
	{
		p = float4(c.bg, K.wz);
	}
	else
	{
		p = float4(c.gb, K.xy);
	}

	float4 q;
	if (c.r < p.x)
	{
		q = float4(p.xyw, c.r);
	}
	else
	{
		q = float4(c.r, p.yzx);
	}

	float d = q.x - min(q.w, q.y);
	float e = 1e-6;

	float3 hsv;
	hsv.x = abs(q.z + (q.w - q.y) / (6.0 * d + e));
	hsv.y = d / (q.x + e);
	hsv.z = q.x;
	return hsv;
}

static float3 HsvToRgb(float3 hsv)
{
	float h = hsv.x * 6.0;
	float i = floor(h);
	float f = h - i;

	float p = hsv.z * (1.0 - hsv.y);
	float q = hsv.z * (1.0 - hsv.y * f);
	float t = hsv.z * (1.0 - hsv.y * (1.0 - f));

	if (i < 1.0)
	{
		return float3(hsv.z, t, p);
	}
	if (i < 2.0)
	{
		return float3(q, hsv.z, p);
	}
	if (i < 3.0)
	{
		return float3(p, hsv.z, t);
	}
	if (i < 4.0)
	{
		return float3(p, q, hsv.z);
	}
	if (i < 5.0)
	{
		return float3(t, p, hsv.z);
	}

	return float3(hsv.z, p, q);
}

// -----------------------------------------------------------------------------
// Drying color grading
// -----------------------------------------------------------------------------
static float3 ApplyGrassDrying(float3 baseColor, float dry01)
{
	dry01 = saturate(dry01);
	dry01 = pow(dry01, 0.65);

	float3 hsv = RgbToHsv(baseColor);

	// Saturation drop + value darken
	hsv.y *= lerp(1.0, (1.0 - g_GrassCB.DrySaturationReduct), dry01);
	hsv.z *= lerp(1.0, (1.0 - g_GrassCB.DryDarken), dry01);

	float3 c0 = HsvToRgb(hsv);

	// Warm tint blend
	float3 c1 = c0 * g_GrassCB.DryTint;

	return lerp(c0, c1, dry01);
}

// -----------------------------------------------------------------------------
// Height ramp helper
// - height01 <= h0 : 0
// - h0..h1         : linear 0..1
// - height01 >= h1 : 1
// -----------------------------------------------------------------------------
static float ComputeHeightRamp01(float height01, float h0, float h1)
{
	float denom = max(h1 - h0, 1e-6);
	return saturate((height01 - h0) / denom);
}

// -----------------------------------------------------------------------------
// Erosion progression (depth-space 0..1) used by both:
// - Mesh path: can clip / alpha-reduce
// - CrossPlane/Billboard path: color-only drying
//
// Inputs
// - edgeDist01: 0 at boundary, 1 deep inside
// - erosion01: global erosion front in 0..1
// - depthEff : edgeDist01 + noise
//
// Output
// - ePix: 0..1, increases when erosion surpasses local depthEff
// -----------------------------------------------------------------------------
static float ComputeErosionEPix(
	Texture2D<float> edgeDistanceTex,
	float2 uv,
	float erosionStrength01,
	float erosionMaxDist01,
	float erosionSmoothness,
	float noiseScale,
	float noiseAmount)
{
	float insideDepth01 = edgeDistanceTex.Sample(g_LinearClampSampler, uv).r;

	float n01 = g_PerlinNoiseTex.Sample(g_LinearWrapSampler, uv * noiseScale).r;

	float depthNoise = (n01 - 0.5) * 2.0 * noiseAmount;
	float depthEff = saturate(insideDepth01 + depthNoise);

	float erosion = saturate(erosionStrength01) * erosionMaxDist01;

	float smoothW = max(erosionSmoothness, 1e-4);
	float ePix = saturate((erosion - depthEff) / smoothW);

	return ePix;
}

// -----------------------------------------------------------------------------
// Coverage shaping used for root blend stability
// -----------------------------------------------------------------------------
static float ComputeCoverageMask(float coverage, float start)
{
	float m = saturate((coverage - start) / max(1.0 - start, 1e-6));
	m *= m;
	return m;
}

// -----------------------------------------------------------------------------
// Root blend factor (0..1)
// - rootMask : stronger near root (height01 small)
// - covMask  : stronger when coverage is high (avoid halo when alpha is low)
// -----------------------------------------------------------------------------
static float ComputeRootBlend(float height01, float coverage)
{
	float rootMask = 1.0 - saturate(height01);
	rootMask *= rootMask;

	float covMask = ComputeCoverageMask(coverage, 0.25);

	return rootMask * covMask;
}

// -----------------------------------------------------------------------------
// Apply terrain tint + AO boost near root
// -----------------------------------------------------------------------------
static void ApplyRootTerrainBlend(
	float3 worldPos,
	float blend01,
	inout float3 baseColor,
	inout float ao)
{
	float3 terrainTint = SampleTerrainDiffuseAtWorldXZLevel(worldPos.xz, /*mip*/5).rgb;
	baseColor = lerp(baseColor, terrainTint, blend01 * 0.35);

	const float ROOT_AO_STRENGTH = 0.65;
	const float ROOT_AO_MIN = 0.25;

	float aoRoot = lerp(ao, ao * (1.0 - ROOT_AO_STRENGTH), blend01);
	ao = max(aoRoot, ROOT_AO_MIN);
}

#endif // GRASS_COMMON_HLSLI
