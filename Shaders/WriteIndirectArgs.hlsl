#include "HLSL_Structures.hlsli"

// 20 bytes: D3D12_DRAW_INDEXED_ARGUMENTS layout
RWByteAddressBuffer g_IndirectArgs;

// uint Counter (4 bytes) at byte offset 0
RWByteAddressBuffer g_Counter;

static const uint MAX_INSTANCES = 1u << 24;

// ----------------------------------------------------------------------------
// Indirect args
// ----------------------------------------------------------------------------
static const uint INDEX_COUNT_PER_INSTANCE = 39;
static const uint START_INDEX_LOCATION = 0;
static const uint BASE_VERTEX_LOCATION = 0;
static const uint START_INSTANCE_LOCATION = 0;

[numthreads(1, 1, 1)]
void WriteIndirectArgs(uint3 tid : SV_DispatchThreadID)
{
    uint instanceCount = g_Counter.Load(0);
    instanceCount = min(instanceCount, MAX_INSTANCES);

    // D3D12_DRAW_INDEXED_ARGUMENTS:
    //   uint IndexCountPerInstance
    //   uint InstanceCount
    //   uint StartIndexLocation
    //   int  BaseVertexLocation
    //   uint StartInstanceLocation
    g_IndirectArgs.Store(0, INDEX_COUNT_PER_INSTANCE);
    g_IndirectArgs.Store(4, instanceCount);
    g_IndirectArgs.Store(8, START_INDEX_LOCATION);
    g_IndirectArgs.Store(12, BASE_VERTEX_LOCATION);
    g_IndirectArgs.Store(16, START_INSTANCE_LOCATION);
}
