#include "Common.hlsli"

Texture2D<float> g_Src;
RWTexture2D<float> g_Dst;

#ifndef BLUR_GROUP_SIZE_X
#define BLUR_GROUP_SIZE_X 8
#endif

#ifndef BLUR_GROUP_SIZE_Y
#define BLUR_GROUP_SIZE_Y 8
#endif

[numthreads(BLUR_GROUP_SIZE_X, BLUR_GROUP_SIZE_Y, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint w = (uint) g_FrameCB.ViewportSize.x;
	uint h = (uint) g_FrameCB.ViewportSize.y;

	if (tid.x >= w || tid.y >= h)
	{
		return;
	}

	// pixel center uv
	float2 uv = (float2(tid.xy) + 0.5f) * g_FrameCB.InvViewportSize;

	// texel size in uv
	float2 texel = g_FrameCB.InvViewportSize;

	// Bilinear taps:
	// center (no offset) + 4 diagonals
	// Diagonal taps at +/- 1 texel with linear sampling provide a cheap smooth blur.
	float c = g_Src.SampleLevel(g_LinearClampSampler, uv, 0);

	float d0 = g_Src.SampleLevel(g_LinearClampSampler, uv + texel * float2(-1.0, -1.0), 0);
	float d1 = g_Src.SampleLevel(g_LinearClampSampler, uv + texel * float2(+1.0, -1.0), 0);
	float d2 = g_Src.SampleLevel(g_LinearClampSampler, uv + texel * float2(-1.0, +1.0), 0);
	float d3 = g_Src.SampleLevel(g_LinearClampSampler, uv + texel * float2(+1.0, +1.0), 0);

	// weights (sum = 1)
	float outV = c * 0.5f + (d0 + d1 + d2 + d3) * 0.125f;

	g_Dst[tid.xy] = saturate(outV);
}
