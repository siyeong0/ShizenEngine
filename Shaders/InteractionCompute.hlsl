#include "HLSL_Structures.hlsli"

// ------------------------------------------------------------
// Constant buffers
// ------------------------------------------------------------
cbuffer INTERACTION_CONSTANTS
{
	InteractionConstants g_InteractionCB;
};

cbuffer INTERACTION_DISPATCH
{
	InteractionDispatch g_InterDisp;
};

// ------------------------------------------------------------
// Resources
// ------------------------------------------------------------
RWTexture2D<float> g_RWInteractionField; // R16_FLOAT UAV as float read/write
StructuredBuffer<InteractionStamp> g_Stamps;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
uint2 WrapCoord(uint2 p, uint2 size)
{
	return uint2(p.x % size.x, p.y % size.y);
}

// local texel [0..W) -> ring texel coord applying TexelOrigin
uint2 LocalToRing(uint2 local)
{
	uint2 size = uint2(g_InteractionCB.FieldWidth, g_InteractionCB.FieldHeight);
	return WrapCoord(g_InteractionCB.TexelOrigin + local, size);
}

// local texel -> worldXZ (meters) within field window
float2 LocalTexelToWorldXZ(uint2 local)
{
	float2 sizeF = float2(g_InteractionCB.FieldWidth, g_InteractionCB.FieldHeight);
	float2 uv = (float2(local) + 0.5.xx) / sizeF; // 0..1 in local window
	return g_InteractionCB.FieldOriginXZ + uv * g_InteractionCB.FieldWorldSizeXZ;
}

float StampFalloff01(float dist01, float falloffPower)
{
	float t = saturate(1.0f - dist01);
	return pow(t, max(falloffPower, 1e-3));
}

// ------------------------------------------------------------
// Pass A) Decay (full field, ring-space direct)
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void DecayInteractionField(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x >= g_InteractionCB.FieldWidth || tid.y >= g_InteractionCB.FieldHeight)
	{
		return;
	}

	float v = g_RWInteractionField[int2(tid.xy)];
	v = max(v - g_InteractionCB.DecayPerSec * g_InteractionCB.DeltaTime, 0.0f);
	v = clamp(v, g_InteractionCB.ClampMin, g_InteractionCB.ClampMax);
	g_RWInteractionField[int2(tid.xy)] = v;
}

// ------------------------------------------------------------
// Pass B1) ClearRect (dispatch domain = RectSize, LOCAL space)
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void ClearInteractionRect(uint3 tid : SV_DispatchThreadID)
{
	uint2 localInRect = tid.xy;
	if (localInRect.x >= g_InterDisp.RectSize.x || localInRect.y >= g_InterDisp.RectSize.y)
	{
		return;
	}

	uint2 localTexel = g_InterDisp.RectOffset + localInRect; // local field texel
	uint2 ringTexel = LocalToRing(localTexel);

	g_RWInteractionField[int2(ringTexel)] = 0.0f;
}

// ------------------------------------------------------------
// Pass B2) ApplySingleStamp (dispatch domain = RectSize, LOCAL)
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void ApplyInteractionStampRect(uint3 tid : SV_DispatchThreadID)
{
	uint2 localInRect = tid.xy;
	if (localInRect.x >= g_InterDisp.RectSize.x || localInRect.y >= g_InterDisp.RectSize.y)
	{
		return;
	}

	// ApplySingleStamp
	uint si = g_InterDisp.StampIndex;
	if (si >= g_InteractionCB.NumStamps)
	{
		return;
	}

	uint2 localTexel = g_InterDisp.RectOffset + localInRect; // local field texel
	uint2 ringTexel = LocalToRing(localTexel);

	InteractionStamp s = g_Stamps[si];

	float2 worldXZ = LocalTexelToWorldXZ(localTexel);

	float outV = g_RWInteractionField[int2(ringTexel)];

	float r = max(s.Radius, 1e-6f);
	float2 d = (worldXZ - s.CenterXZ);
	float dist01 = length(d) / r;

	if (dist01 < 1.0f)
	{
		float f = StampFalloff01(dist01, s.FalloffPower);
		float w = saturate(f * s.Strength);

		if ((s.Flags & INTERACTION_STAMP_SUBTRACT) != 0)
		{
			outV = max(outV - w, 0.0f);
		}
		else if ((s.Flags & INTERACTION_STAMP_MAX_BLEND) != 0)
		{
			// outV = max(outV, w);
			outV = lerp(outV, 1.0f, w);
		}
		else
		{
			outV = lerp(outV, 1.0f, w);
		}
	}

	outV = clamp(outV, g_InteractionCB.ClampMin, g_InteractionCB.ClampMax);
	g_RWInteractionField[int2(ringTexel)] = outV;
}
