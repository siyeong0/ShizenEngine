#include "Common.hlsli"
#include "GrassCommon.hlsli"

StructuredBuffer<GrassBillboardInstance> g_GrassInstances;

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

void main(BaseVSInput IN, out BaseVSOutput OUT, uint instanceID : SV_InstanceID)
{
    GrassBillboardInstance rawInst = g_GrassInstances[instanceID];

	float3 vertexPosition = GET_VERTEX_POS();
	float2 vertexUV = GET_VERTEX_UV();
    
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

	float x = vertexPosition.x * scale;
	float y = vertexPosition.y * scale;

    float3 worldPos = posWS + camR * x + camU * y;

    // Billboard normal: face camera (or -camF depending on convention)
    float3 worldNormal = NormalizeSafe3(-camF, float3(0.0f, 0.0f, 1.0f));

    // Tangent for normal mapping / GBuffer input completeness:
    // Choose camera right as stable tangent.
    float3 worldTangent = NormalizeSafe3(camR, float3(1.0f, 0.0f, 0.0f));

	SET_VSOUT_WORLD_POS_STATIC(float4(worldPos, 1.0f));
    SET_VSOUT_UV(vertexUV);
    SET_VSOUT_WORLD_NORMAL(worldNormal);
    SET_VSOUT_WORLD_TANGENT(worldTangent);
}
