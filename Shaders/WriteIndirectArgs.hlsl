#include "HLSL_Structures.hlsli"

static const uint INDIRECT_ARGS_STRIDE_BYTES = 20u;
static const uint INDIRECT_COUNT_STRIDE_BYTES = 4u;

// Constants
cbuffer INDIRECT_ARGS_WRITER_CB
{
    IndirectConstants g_IndirectCB;
};

RWByteAddressBuffer g_IndirectArgs;
RWByteAddressBuffer g_IndirectCounts;

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------
static uint ClampInstanceCount(uint count, uint maxCount)
{
    // If maxCount == 0 -> treat as "no clamp" (optional)
    if (maxCount == 0u)
    {
        return count;
    }

    return min(count, maxCount);
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------
[numthreads(64, 1, 1)]
void WriteIndirectArgs(uint3 tid : SV_DispatchThreadID)
{
    uint slot = tid.x;

    // Out of bounds
    if (slot >= g_IndirectCB.NumSlots || slot >= MAX_NUM_INDIRECTS)
    {
        return;
    }

    // Read instance count for this slot (written by instance build kernel)
    uint instanceCount = g_IndirectCounts.Load(slot * INDIRECT_COUNT_STRIDE_BYTES);
    instanceCount = ClampInstanceCount(instanceCount, g_IndirectCB.MaxInstances);

    // Read template for this slot
    IndirectArgsTemplate t = g_IndirectCB.Templates[slot];

    // Write args at slot * 20
    uint base = slot * INDIRECT_ARGS_STRIDE_BYTES;
    g_IndirectArgs.Store(base + 0u, t.IndexCountPerInstance);
    g_IndirectArgs.Store(base + 4u, instanceCount);
    g_IndirectArgs.Store(base + 8u, t.StartIndexLocation);
    g_IndirectArgs.Store(base + 12u, t.BaseVertexLocation);
    g_IndirectArgs.Store(base + 16u, t.StartInstanceLocation);
    
    // Reset counter
    g_IndirectCounts.Store(slot * INDIRECT_COUNT_STRIDE_BYTES, 0);
}
