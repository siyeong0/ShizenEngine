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

// Batch data (CPU-built)
struct InteractionBatchDesc
{
    uint Offset; // start index in g_BatchStampIndices
    uint Count;  // number of stamps in this batch
    uint2 _Pad;
};

StructuredBuffer<InteractionBatchDesc> g_Batches;
StructuredBuffer<uint>                g_BatchStampIndices;

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
    float2 uv = (float2(local)+0.5.xx) / sizeF; // 0..1 in local window
    return g_InteractionCB.FieldOriginXZ + uv * g_InteractionCB.FieldWorldSizeXZ;
}

float StampFalloff01(float dist01, float falloffPower)
{
    float t = saturate(1.0f - dist01);
    return pow(t, max(falloffPower, 1e-3));
}

// cheap hash for "random pick"
uint HashU32(uint x)
{
    // Wang hash-ish
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x = x ^ (x >> 4);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

uint Hash3(uint a, uint b, uint c)
{
    uint h = HashU32(a);
    h ^= HashU32(b + 0x9e3779b9u);
    h ^= HashU32(c + 0x85ebca6bu);
    return HashU32(h);
}

// ------------------------------------------------------------
// Pass A) Decay (full field, ring-space direct)
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
// Pass B1) ClearRect (dispatch domain = RectSize, LOCAL space)
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void ClearInteractionRect(uint3 tid : SV_DispatchThreadID)
{
    uint2 localInRect = tid.xy;
    if (localInRect.x >= g_InterDisp.RectSize.x || localInRect.y >= g_InterDisp.RectSize.y)
        return;

    uint2 localTexel = g_InterDisp.RectOffset + localInRect; // local field texel
    uint2 ringTexel = LocalToRing(localTexel);

    g_RWInteractionField[int2(ringTexel)] = 0.0f;
}

// ------------------------------------------------------------
// Pass B2) ApplyBatchRect (dispatch domain = RectSize, LOCAL)
// - StampIndex is repurposed as BatchIndex
// - For pixels affected by multiple stamps in the batch, pick ONE via hash
// ------------------------------------------------------------
[numthreads(8, 8, 1)]
void ApplyInteractionBatchRect(uint3 tid : SV_DispatchThreadID)
{
    uint2 localInRect = tid.xy;
    if (localInRect.x >= g_InterDisp.RectSize.x || localInRect.y >= g_InterDisp.RectSize.y)
        return;

    uint2 localTexel = g_InterDisp.RectOffset + localInRect; // local field texel
    uint2 ringTexel = LocalToRing(localTexel);

    float outV = g_RWInteractionField[int2(ringTexel)];

    // Batch
    uint batchIndex = g_InterDisp.StampIndex;
    InteractionBatchDesc bd = g_Batches[batchIndex];

    float2 worldXZ = LocalTexelToWorldXZ(localTexel);

    // Choose exactly one stamp among those that affect this pixel.
    // We select the stamp with the smallest hash key (deterministic "random").
    bool  bHasPick = false;
    uint  bestKey = 0xFFFFFFFFu;
    InteractionStamp bestStamp = (InteractionStamp)0;

    [loop]
    for (uint i = 0; i < bd.Count; ++i)
    {
        uint si = g_BatchStampIndices[bd.Offset + i];
        if (si >= g_InteractionCB.NumStamps)
            continue;

        InteractionStamp s = g_Stamps[si];

        float r = max(s.Radius, 1e-6f);
        float2 d = (worldXZ - s.CenterXZ);
        float dist01 = length(d) / r;

        if (dist01 < 1.0f)
        {
            uint key = Hash3(localTexel.x, localTexel.y, si);
            if (!bHasPick || key < bestKey)
            {
                bHasPick = true;
                bestKey = key;
                bestStamp = s;
            }
        }
    }

    if (bHasPick)
    {
        InteractionStamp s = bestStamp;

        float r = max(s.Radius, 1e-6f);
        float2 d = (worldXZ - s.CenterXZ);
        float dist01 = length(d) / r;

        // dist01 is guaranteed < 1.0f here, but recompute is cheap and avoids storing dist.
        float f = StampFalloff01(dist01, s.FalloffPower);
        float w = saturate(f * s.Strength);

        if ((s.Flags & INTERACTION_STAMP_SUBTRACT) != 0)
        {
            outV = max(outV - w, 0.0f);
        }
        else if ((s.Flags & INTERACTION_STAMP_MAX_BLEND) != 0)
        {
            // choose your policy
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
