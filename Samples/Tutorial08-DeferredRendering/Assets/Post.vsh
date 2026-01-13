#include "HLSL_Common.hlsli"

void main(in uint VertID : SV_VertexID, out FullscreenVSOut VSOut)
{
    float2 uv = float2((VertID & 1) ? 1.0 : 0.0, (VertID & 2) ? 1.0 : 0.0);
    VSOut.UV = uv;

    float2 pos = uv * 2.0 - 1.0;
    VSOut.Pos = float4(pos.x, -pos.y, 0.0, 1.0);
}
