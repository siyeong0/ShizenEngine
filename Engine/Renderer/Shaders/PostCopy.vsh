// PostCopy.vsh
#include "HLSL_Structures.hlsli"

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

void main(in uint VertId : SV_VertexID, out VSOutput VSOut)
{
    // Vert positions in clip space (fullscreen triangle)
    // ( -1, -1 ), ( 3, -1 ), ( -1, 3 )
    float2 pos;
    if (VertId == 0)
        pos = float2(-1.0, -1.0);
    else if (VertId == 1)
        pos = float2(3.0, -1.0);
    else
        pos = float2(-1.0, 3.0);

    VSOut.Pos = float4(pos, 0.0, 1.0);
    VSOut.UV = pos * 0.5 + 0.5;
}
