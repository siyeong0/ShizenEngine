#include "HLSL_Structures.hlsli"

cbuffer INTERACTION_CONSTANTS
{
    InteractionConstants g_InteractionCB;
};

// R16_FLOAT texture as float
RWTexture2D<float> g_RWInteractionField;

// Read-only stamps (uploaded from CPU)
StructuredBuffer<InteractionStamp> g_Stamps;

SamplerState g_LinearClampSampler;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
float StampFalloff(float dist01, float falloffPower)
{
    // dist01: 0 at center, 1 at radius
    float t = saturate(1.0f - dist01);
    // falloffPower: 1=linear, 2=quadratic, ...
    return pow(t, max(falloffPower, 1e-3));
}

// ------------------------------------------------------------
// Pass 1) Decay whole field
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void DecayInteractionField(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_InteractionCB.FieldWidth || tid.y >= g_InteractionCB.FieldHeight)
        return;

    float v = g_RWInteractionField[int2(tid.xy)];

    // Exponential-ish linear decay (simple & stable)
    v = max(v - g_InteractionCB.DecayPerSec * g_InteractionCB.DeltaTime, 0.0f);
    v = clamp(v, g_InteractionCB.ClampMin, g_InteractionCB.ClampMax);

    g_RWInteractionField[int2(tid.xy)] = v;
}

// ------------------------------------------------------------
// Pass 2) Apply stamps (per-pixel loop over stamps)
// - stamps count is expected to be small (e.g. <= 128)
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void ApplyInteractionStamps(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_InteractionCB.FieldWidth || tid.y >= g_InteractionCB.FieldHeight)
        return;

    // Pixel -> world mapping:
    // We assume InteractionField is in the SAME UV space as terrain heightmap (0..1).
    // So pixel uv = (x+0.5)/W, (y+0.5)/H.
    float2 uv = (float2(tid.x + 0.5f, tid.y + 0.5f) / float2(g_InteractionCB.FieldWidth, g_InteractionCB.FieldHeight));

    // Convert to world XZ with the SAME mapping you used in GrassBuildInstances:
    // We can’t access GrassGenConstants here (different CB), so we assume:
    // - CPU builds stamps in world XZ
    // - CPU also passes terrain origin/size OR you keep stamps already normalized.
    //
    // 권장: Stamp의 CenterXZ를 “world”가 아니라 “terrain-uv(0..1)”로 넣는 방식.
    // 그러면 여기서 world 변환이 필요 없고, 바로 uv거리로 계산 가능.
    //
    // 아래는 “CenterXZ가 UV(0..1)”라고 가정한 버전:
    float2 p = uv;

    float outV = g_RWInteractionField[int2(tid.xy)];

    [loop]
    for (uint i = 0; i < g_InteractionCB.NumStamps; ++i)
    {
        InteractionStamp s = g_Stamps[i];

        // CenterXZ: UV space(0..1)라고 가정
        float2 d = p - s.CenterXZ;

        // Radius도 UV 스케일이라고 가정 (ex: 0.01 ~ 0.05)
        float r = max(s.Radius, 1e-6);
        float dist = length(d);
        if (dist >= r)
            continue;

        float f = StampFalloff(dist / r, s.FalloffPower);
        float delta = f * s.Strength;

        if ((s.Flags & INTERACTION_STAMP_SUBTRACT) != 0)
        {
            outV = max(outV - delta, 0.0f);
        }
        else if ((s.Flags & INTERACTION_STAMP_MAX_BLEND) != 0)
        {
            outV = max(outV, delta);
        }
        else
        {
            // default: additive then clamp
            outV += delta;
        }
    }

    outV = clamp(outV, g_InteractionCB.ClampMin, g_InteractionCB.ClampMax);
    g_RWInteractionField[int2(tid.xy)] = outV;
}
