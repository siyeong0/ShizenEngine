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

    float2 uv = (float2(tid.x + 0.5f, tid.y + 0.5f) /
                 float2(g_InteractionCB.FieldWidth, g_InteractionCB.FieldHeight));

    float2 p = uv;

    float outV = g_RWInteractionField[int2(tid.xy)];

    [loop]
    for (uint i = 0; i < g_InteractionCB.NumStamps; ++i)
    {
        InteractionStamp s = g_Stamps[i];

        float2 d = p - s.CenterXZ;

        float r = max(s.Radius, 1e-6);
        float dist = length(d);

        // 밖이면 영향 없음
        if (dist >= r)
            continue;

        // 0..1 마스크 (가장자리 부드럽게)
        float f = StampFalloff(dist / r, s.FalloffPower);

        if ((s.Flags & INTERACTION_STAMP_SUBTRACT) != 0)
        {
            // "감소"는 기존처럼 천천히 하고 싶다면 굳이 stamp로 빼지 말고 decay에 맡기는 게 더 안정적.
            // 그래도 필요하면: outV = max(outV - f * s.Strength, 0.0f);
            outV = max(outV - f * s.Strength, 0.0f);
        }
        else
        {
            // -------------------------------
            // 증가할 때: '무조건 1.0' 스냅
            // -------------------------------
            // f를 마스크로 써서 "영역 가장자리"만 부드럽게.
            // f가 1이면 즉시 1, f가 0.2면 outV를 1로 끌어올리는 비율 0.2
            // (프레임레이트/스탬프 수에 덜 민감)
            float snap = saturate(f * s.Strength); // Strength를 마스크 강도로 사용 (보통 1)
            outV = lerp(outV, 1.0f, snap);

            // 만약 “영역 안이면 무조건 1”을 진짜 원하면(완전 스냅):
            // if (snap > 0.001f) outV = 1.0f;
        }
    }

    outV = clamp(outV, g_InteractionCB.ClampMin, g_InteractionCB.ClampMax);
    g_RWInteractionField[int2(tid.xy)] = outV;
}
