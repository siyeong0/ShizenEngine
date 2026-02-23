#define ADDITIONAL_VS_OUT_FIELDS \
	VS_OUT_FIELD(float, Height01, 0)

#include "Common.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<uint> g_SpeciesLodOffsets;
StructuredBuffer<GrassMeshInstance> g_GrassInstances;

// -----------------------------------------------------------------------------
// Wind application helper
// -----------------------------------------------------------------------------
static void ApplyGrassWindVS(
	in float3 posWS,
	in float scale,
	in float yaw,
	in float bend01,
	in float pressHard,
	in float keepBase,
	in float wTip,
	in uint seed8,
	inout float3 p,
	inout float3 n,
	inout float3 t,
	in float currTime
)
{
	// Wind dir (world)
	float2 windDir2 = NormalizeSafe2(g_GrassCB.WindDirXZ, float2(1.0f, 0.0f));
	float3 windDirWS = float3(windDir2.x, 0.0f, windDir2.y);

	// Small per-instance yaw-based jitter for direction
	static const float WIND_DIR_JITTER = 0.20f;

	float3 windDirJittered = ApplyYaw(windDirWS, (yaw - GRASS_PI) * WIND_DIR_JITTER);
	windDirJittered.y = 0.0f;
	windDirJittered = NormalizeSafe3(windDirJittered, windDirWS);

	// Bend axis = up x windDir
	float3 windBendAxis = cross(float3(0.0f, 1.0f, 0.0f), windDirJittered);
	windBendAxis = NormalizeSafe3(windBendAxis, float3(1.0f, 0.0f, 0.0f));

	// Phase (world-space) + time + yaw seed
	float phase =
		dot(posWS.xz, windDir2) * g_GrassCB.WindFreq +
		currTime * g_GrassCB.WindSpeed +
		yaw * 0.37f;

	// Gust modulation
	float gust =
		1.0f +
		g_GrassCB.WindGust *
		sin(currTime * (g_GrassCB.WindSpeed * 0.63f) + yaw);

	// Triangle wave signal (constant-ish angular speed)
	float windSignal = TriangleWaveSigned(phase);

	// Interaction fade: reduce wind under strong press
	float windFade = saturate(g_GrassCB.InteractionWindFade);
	float windKeep = lerp(1.0f, 1.0f - windFade, pressHard);
	windKeep *= keepBase;

	// Angle
	float rawWindAngle = windSignal * gust * bend01 * g_GrassCB.WindStrength;
	rawWindAngle *= windKeep;

	float maxA = max(g_GrassCB.MaxBendAngle, 1e-4f);
	float windAngle = SoftLimitSigned(rawWindAngle, maxA);

	// Tip weighting
	float windTipAngle = windAngle * wTip;

	// Bend rotation
	p = RotateAroundAxis(p, windBendAxis, windTipAngle);
	n = RotateAroundAxis(n, windBendAxis, windTipAngle);
	t = RotateAroundAxis(t, windBendAxis, windTipAngle);

	// Matched positional offset (same signed signal path; ApplyGrassWindPosWS also uses triangle)
	p += ApplyGrassWindPosWS(posWS, scale, yaw, bend01, pressHard, keepBase, wTip, seed8);
}

// -----------------------------------------------------------------------------
// Full transform builder (current OR previous) so dynamic output matches.
// - Keeps exact math order from your original VS.
// -----------------------------------------------------------------------------
static void BuildGrassVertexWS(
	in float3 vertexPosition,
	in float3 vertexNormal,
	in float3 vertexTangent,
	in float3 posWS,
	in float scale,
	in float yaw,
	in float pitch,
	in float bend01,
	in float press,
	in uint seed8,
	in float timeSec,
	out float3 outPws,
	out float3 outNws,
	out float3 outTws
)
{
	float3 p = vertexPosition * scale;
	float3 n = vertexNormal;
	float3 t = vertexTangent;

	// Interaction strength shaping
	float pressHard = smoothstep(0.05f, 0.25f, saturate(press));
	float keepBase = 1.0f - pressHard;

	// Yaw
	p = ApplyYaw(p, yaw);
	n = ApplyYaw(n, yaw);
	t = ApplyYaw(t, yaw);

	// Pitch (keep base under press)
	float3 pitchAxis = ApplyYaw(float3(1.0f, 0.0f, 0.0f), yaw);
	pitchAxis = NormalizeSafe3(pitchAxis, float3(1.0f, 0.0f, 0.0f));

	float pitchAngle = pitch * keepBase;

	p = RotateAroundAxis(p, pitchAxis, pitchAngle);
	n = RotateAroundAxis(n, pitchAxis, pitchAngle);
	t = RotateAroundAxis(t, pitchAxis, pitchAngle);

	// Tip weight (based on original, unscaled vertexPosition.y)
	float height01 = saturate(vertexPosition.y);
	float wTip = height01 * height01;

	// Wind (factored out)
	ApplyGrassWindVS(
		posWS,
		scale,
		yaw,
		bend01,
		pressHard,
		keepBase,
		wTip,
		seed8,
		p,
		n,
		t,
		timeSec
	);

	// Interaction axis (flatten)
	float2 pressDir2 = float2(cos(yaw), sin(yaw));
	float3 pressDirWS = float3(pressDir2.x, 0.0f, pressDir2.y);

	float3 pressAxis = cross(float3(0.0f, 1.0f, 0.0f), pressDirWS);
	pressAxis = NormalizeSafe3(pressAxis, float3(1.0f, 0.0f, 0.0f));

	// Interaction flatten + sink
	float targetFlat = max(g_GrassCB.InteractionBendAngle, 0.0f);
	float flattenAngle = targetFlat * pressHard;

	p = RotateAroundAxis(p, pressAxis, flattenAngle * pressHard);
	n = RotateAroundAxis(n, pressAxis, flattenAngle * pressHard);
	t = RotateAroundAxis(t, pressAxis, flattenAngle * pressHard);

	p.y -= pressHard * g_GrassCB.InteractionSink;

	// Final translate
	p += posWS;

	outPws = p;
	outNws = NormalizeSafe3(n, float3(0.0f, 1.0f, 0.0f));
	outTws = NormalizeSafe3(t, float3(1.0f, 0.0f, 0.0f));
}

// -----------------------------------------------------------------------------
// VS
// -----------------------------------------------------------------------------
BASE_VS_MAIN_ENTRY(InstanceID)
{
	uint baseInstance = g_SpeciesLodOffsets[g_DrawCB.StartInstanceLocation];
	uint globalInstanceId = InstanceID + baseInstance;
	GrassMeshInstance rawInst = g_GrassInstances[globalInstanceId];

	float3 vertexPosition = GET_VERTEX_POS();
	float2 vertexUV = GET_VERTEX_UV();
	float3 vertexNormal = GET_VERTEX_NORMAL();
	float3 vertexTangent = GET_VERTEX_TANGENT();

	float3 posWS;
	float scale;
	float yaw;
	float pitch;
	float bend01;
	float press;
	uint variantId;
	uint seed8;

	DecodeGrassMeshInstance(rawInst, posWS, scale, yaw, pitch, bend01, press, variantId, seed8);

	// Current (uses g_FrameCB.CurrTime)
	float3 pCurrWS, nCurrWS, tCurrWS;
	BuildGrassVertexWS(
		vertexPosition,
		vertexNormal,
		vertexTangent,
		posWS,
		scale,
		yaw,
		pitch,
		bend01,
		press,
		seed8,
		g_FrameCB.CurrTime,
		pCurrWS,
		nCurrWS,
		tCurrWS
	);

	// Previous (uses g_FrameCB.PrevCurrTime)
	// NOTE: This assumes PrevCurrTime is the "previous frame's CurrTime" in seconds,
	// so wind phase/offset matches temporal reprojection.
	float3 pPrevWS, nPrevWS, tPrevWS;
	BuildGrassVertexWS(
		vertexPosition,
		vertexNormal,
		vertexTangent,
		posWS,
		scale,
		yaw,
		pitch,
		bend01,
		press,
		seed8,
		g_FrameCB.PrevCurrTime,
		pPrevWS,
		nPrevWS,
		tPrevWS
	);
	
	// Outputs (dynamic)
	SET_VSOUT_WORLD_POS_DYNAMIC(float4(pCurrWS, 1.0f), float4(pPrevWS, 1.0f));
	SET_VSOUT_UV(vertexUV);
	SET_VSOUT_WORLD_NORMAL(nCurrWS);
	SET_VSOUT_WORLD_TANGENT(tCurrWS);
	
	float height01 = saturate(vertexPosition.y);
	SET_VSOUT_ADDITIONAL(Height01, height01);
}