#include "Common.hlsli"

Texture2D<float> g_Src; // AO input (0..1)
Texture2D<float> g_Depth; // D3D depth 0..1
Texture2D<float4> g_Normal; // packed 0..1 (world)

RWTexture2D<float> g_Dst;

// -----------------------------------------------------------------------------
// Tunables
// -----------------------------------------------------------------------------
static const int AO_BLUR_RADIUS_PX = 4; // 2~6
static const float AO_DEPTH_SIGMA = 1.5; // larger = blur across depth edges more
static const float AO_NORMAL_SIGMA = 0.35; // larger = blur across normal edges more

static float SpatialWeight(int x)
{
    const float r = (float) AO_BLUR_RADIUS_PX;
    const float d = (float) (x * x);
    return exp(-d / max(r * r, 1.0));
}

static float DepthWeight(float d0, float d1)
{
    float dz = abs(d1 - d0); // non-linear depth, still good as edge hint
    return exp(-dz * (1.0 / max(AO_DEPTH_SIGMA, 1e-5)));
}

static float NormalWeight(float3 n0, float3 n1)
{
    float nd = saturate(dot(n0, n1));
    float t = saturate((nd - (1.0 - AO_NORMAL_SIGMA)) / max(AO_NORMAL_SIGMA, 1e-5));
    return t;
}

// Robust axis selection:
// - BlurX build defines AO_BLUR_HORIZONTAL
// - BlurY build defines AO_BLUR_VERTICAL
// - If none defined, default to horizontal (safe fallback)
static int2 AxisOffset(int i)
{
#if defined(AO_BLUR_VERTICAL)
    return int2(0, i);
#else
    return int2(i, 0);
#endif
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint w = (uint) g_FrameCB.ViewportSize.x;
    uint h = (uint) g_FrameCB.ViewportSize.y;

    if (dtid.x >= w || dtid.y >= h)
        return;

    uint2 p = dtid.xy;
    float2 uv0 = (float2(p) + 0.5) * g_FrameCB.InvViewportSize;

    float ao0 = g_Src.SampleLevel(g_PointClampSampler, uv0, 0);
    float d0 = g_Depth.SampleLevel(g_PointClampSampler, uv0, 0);
    float3 n0 = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uv0, 0).xyz);

    float sumW = 0.0;
    float sumA = 0.0;

    [loop]
    for (int i = -AO_BLUR_RADIUS_PX; i <= AO_BLUR_RADIUS_PX; ++i)
    {
        int2 of = AxisOffset(i);
        int2 pi = int2((int) p.x + of.x, (int) p.y + of.y);

        pi.x = clamp(pi.x, 0, (int) w - 1);
        pi.y = clamp(pi.y, 0, (int) h - 1);

        float2 uvi = (float2(pi) + 0.5) * g_FrameCB.InvViewportSize;

        float a = g_Src.SampleLevel(g_LinearClampSampler, uvi, 0);
        float d1 = g_Depth.SampleLevel(g_PointClampSampler, uvi, 0);
        float3 n1 = UnpackNormal01(g_Normal.SampleLevel(g_PointClampSampler, uvi, 0).xyz);

        float wS = SpatialWeight(i);
        float wD = DepthWeight(d0, d1);
        float wN = NormalWeight(n0, n1);

        float wAll = wS * wD * wN;

        sumW += wAll;
        sumA += a * wAll;
    }

    float outAO = (sumW > 1e-6) ? (sumA / sumW) : ao0;
    g_Dst[p] = outAO;
}
