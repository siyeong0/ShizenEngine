#include "Common.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<GrassBillboardInstance> g_GrassInstances;

struct VSInput
{
    float3 Pos     : ATTRIB0; // local quad corner: x [-0.5..0.5], y [0..1], z 0
    float2 UV      : ATTRIB1;
    float3 Normal  : ATTRIB2;
    float3 Tangent : ATTRIB3;
};

struct VSOutput
{
	float4 SVPosition : SV_POSITION;
	float4 CurrClip : TEXCOORD0;
	float4 PrevClip : TEXCOORD1;
	float2 UV : TEXCOORD2;
	float3 WorldPos : TEXCOORD3;
	float3 WorldN : TEXCOORD4;
	float3 WorldT : TEXCOORD5;
};

void GetCameraBasisWS(out float3 rightWS, out float3 upWS, out float3 forwardWS)
{
    rightWS = float3(g_FrameCB.View._11, g_FrameCB.View._21, g_FrameCB.View._31);
    upWS = float3(g_FrameCB.View._12, g_FrameCB.View._22, g_FrameCB.View._32);
    forwardWS = float3(g_FrameCB.View._13, g_FrameCB.View._23, g_FrameCB.View._33);

    rightWS = NormalizeSafe3(rightWS, float3(1.0f, 0.0f, 0.0f));
    upWS = NormalizeSafe3(upWS, float3(0.0f, 1.0f, 0.0f));
    forwardWS = NormalizeSafe3(forwardWS, float3(0.0f, 0.0f, 1.0f));
}

float3 RotateAroundUp(float3 v, float3 upWS, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);
    float3 a = upWS;
    return v * c + cross(a, v) * s + a * dot(a, v) * (1.0f - c);
}

VSOutput main(VSInput IN, uint instanceID : SV_InstanceID)
{
    VSOutput OUT;

    GrassBillboardInstance rawInst = g_GrassInstances[instanceID];

    float3 posWS;
    float  scale;
    float  yaw;
    uint   atlasIndex;
    uint   seed8;

    DecodeGrassBillboardInstance(rawInst, posWS, scale, yaw, atlasIndex, seed8);

    float3 camR, camU, camF;
    GetCameraBasisWS(camR, camU, camF);

    // Optional: per-instance yaw around camera up
    // camR = RotateAroundUp(camR, camU, yaw);

    float x = IN.Pos.x * scale;
    float y = IN.Pos.y * scale;

    float3 worldPos = posWS + camR * x + camU * y;

    // Billboard normal: face camera (or -camF depending on convention)
    float3 Nw = NormalizeSafe3(-camF, float3(0.0f, 0.0f, 1.0f));

    // Tangent for normal mapping / GBuffer input completeness:
    // Choose camera right as stable tangent.
    float3 Tw = NormalizeSafe3(camR, float3(1.0f, 0.0f, 0.0f));

    OUT.UV = IN.UV;
    OUT.WorldPos = worldPos;
    OUT.WorldN = Nw;
    OUT.WorldT = Tw;
    //OUT.CurrClip = mul(float4(worldPos, 1.0f), g_FrameCB.ViewProjNoJitter);
	//OUT.PrevClip = mul(float4(worldPos, 1.0f), g_FrameCB.PrevViewProjNoJitter);
	OUT.CurrClip = ApplyTAAJittering(mul(float4(worldPos, 1.0f), g_FrameCB.ViewProj));
	OUT.PrevClip = mul(float4(worldPos, 1.0f), g_FrameCB.PrevViewProj);
	OUT.SVPosition = mul(float4(worldPos, 1.0f), g_FrameCB.ViewProj);
    
    return OUT;
}
