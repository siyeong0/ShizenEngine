#include "HLSL_Structures.hlsli"
#include "HeightField.hlsli"

// ------------------------------------------------------------
// Constant buffers
// ------------------------------------------------------------
cbuffer INTERACTION_CONSTANTS
{
    InteractionConstants g_InteractionCB;
};

cbuffer HEIGHT_FIELD_CONSTANTS
{
    HeightFieldConstants g_HeightFieldCB;
};

// ------------------------------------------------------------
// Resources
// ------------------------------------------------------------
// Interaction field is DOMAIN-sized texture (FieldWidth x FieldHeight).
// Each pixel corresponds to a point on terrain domain.
// We convert pixel -> domain uv -> worldXZ using HeightFieldConstants.
RWTexture2D<float> g_RWInteractionField;

// IMPORTANT: Stamps are in WORLD space (meters).
StructuredBuffer<InteractionStamp> g_Stamps;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
float StampFalloff(float dist01, float falloffPower)
{
    float t = saturate(1.0f - dist01);
    return pow(t, max(falloffPower, 1e-3));
}

float2 InteractionPixelToUV(uint2 pix, uint2 fieldSize)
{
    return (float2(pix) + 0.5f.xx) / float2(fieldSize);
}

// Interaction UV (0..1 domain) -> worldXZ on terrain domain.
float2 InteractionUVToWorldXZ(float2 interUV, HeightFieldConstants hf)
{
    return hf.WorldOriginXZ + interUV * hf.WorldSizeXZ;
}

// ------------------------------------------------------------
// Pass 1) Decay
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void DecayInteractionField(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_InteractionCB.FieldWidth || tid.y >= g_InteractionCB.FieldHeight)
        return;

    float v = g_RWInteractionField[int2(tid.xy)];

    v = max(v - g_InteractionCB.DecayPerSec * g_InteractionCB.DeltaTime, 0.0f);
    v = clamp(v, g_InteractionCB.ClampMin, g_InteractionCB.ClampMax);

    g_RWInteractionField[int2(tid.xy)] = v;
}

// ------------------------------------------------------------
// Pass 2) Apply Stamps (WORLD-space distance)
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void ApplyInteractionStamps(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_InteractionCB.FieldWidth || tid.y >= g_InteractionCB.FieldHeight)
        return;

    const uint2 fieldSize = uint2(g_InteractionCB.FieldWidth, g_InteractionCB.FieldHeight);

    // Pixel -> domain uv -> worldXZ
    float2 interUV = InteractionPixelToUV(tid.xy, fieldSize);
    float2 worldXZ = InteractionUVToWorldXZ(interUV, g_HeightFieldCB);

    float outV = g_RWInteractionField[int2(tid.xy)];

    [loop]
    for (uint i = 0; i < g_InteractionCB.NumStamps; ++i)
    {
        InteractionStamp s = g_Stamps[i];

        // WORLD-space radius (meters)
        float r = max(s.Radius, 1e-6f);

        // WORLD-space delta
        float2 d = (worldXZ - s.CenterXZ);

        float dist01 = length(d) / r;
        if (dist01 >= 1.0f)
            continue;

        float f = StampFalloff(dist01, s.FalloffPower);
        float w = saturate(f * s.Strength);

        if ((s.Flags & INTERACTION_STAMP_SUBTRACT) != 0)
        {
            outV = max(outV - w, 0.0f);
        }
        else if ((s.Flags & INTERACTION_STAMP_MAX_BLEND) != 0)
        {
            outV = lerp(outV, 1.0f, w);
        }
        else
        {
            // default: accumulate toward 1
            outV = lerp(outV, 1.0f, w);
        }
    }

    outV = clamp(outV, g_InteractionCB.ClampMin, g_InteractionCB.ClampMax);
    g_RWInteractionField[int2(tid.xy)] = outV;
}
