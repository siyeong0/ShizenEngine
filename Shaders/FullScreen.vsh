#include "HLSL_Structures.hlsli"

// ----------------------------------------------------------------------------
// Fullscreen triangle vertex shader
// - Generates a fullscreen triangle without a vertex buffer.
// - Outputs UV in [0..1] for post-processing / copy passes.
// ----------------------------------------------------------------------------
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

void main(in uint VertId : SV_VertexID, out VSOutput VSOut)
{
    // Clip-space positions for a fullscreen triangle.
    // Using a single triangle avoids diagonal seam artifacts that can happen with a fullscreen quad.
    float2 pos;
    if (VertId == 0)
    {
        pos = float2(-1.0, -1.0);
    }
    else if (VertId == 1)
    {
        pos = float2(3.0, -1.0);
    }
    else
    {
        pos = float2(-1.0, 3.0);
    }

    VSOut.Pos = float4(pos, 0.0, 1.0);

    // Map clip-space XY [-1..1] to UV [0..1].
    // Note: vertices extend beyond the screen (3.0) so the triangle fully covers the viewport after rasterization.
    VSOut.UV = pos * 0.5 + 0.5;

    // Flip Y so that screen-space UV matches the texture-space convention used by our render targets.
    // This prevents upside-down sampling when mixing fullscreen passes with world-space rendering paths.
    VSOut.UV.y = 1.0 - VSOut.UV.y;
}
