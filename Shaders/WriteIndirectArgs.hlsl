#include "HLSL_Structures.hlsli"

static const uint INDIRECT_ARGS_STRIDE_BYTES = 20u;
static const uint UINT_STRIDE_BYTES = 4u;

cbuffer INDIRECT_ARGS_WRITER_CB
{
	IndirectArgsHeader g_IndirectCB;
};

// Slot -> mesh mapping (SRV)
StructuredBuffer<uint> g_SlotMeshId;

// Slot templates (SRV)
StructuredBuffer<IndirectArgsTemplate> g_Templates;

// Indirect args (RAW 20 bytes per slot)
RWByteAddressBuffer g_IndirectArgs;

// Slot draw-count buffer (ExecuteIndirect count buffer semantics: number of commands to execute)
RWByteAddressBuffer g_DrawCountBuffer;

// Mesh instance-count buffer (written by instance build kernels)
RWByteAddressBuffer g_MeshInstanceCountBuffer;

static uint ClampInstanceCount(uint count, uint maxCount)
{
	if (maxCount == 0u)
	{
		return count;
	}

	return min(count, maxCount);
}

[numthreads(64, 1, 1)]
void WriteIndirectArgs(uint3 tid : SV_DispatchThreadID)
{
	uint slot = tid.x;

	if (slot >= MAX_NUM_INDIRECTS)
	{
		return;
	}

	uint meshId = g_SlotMeshId[slot];

	uint instanceCount = g_MeshInstanceCountBuffer.Load(meshId * UINT_STRIDE_BYTES);
	instanceCount = ClampInstanceCount(instanceCount, g_IndirectCB.MaxInstances);

	uint drawCount = (instanceCount > 0u) ? 1u : 0u;
	g_DrawCountBuffer.Store(slot * UINT_STRIDE_BYTES, drawCount);

	IndirectArgsTemplate t = g_Templates[slot];

	uint base = slot * INDIRECT_ARGS_STRIDE_BYTES;
	g_IndirectArgs.Store(base + 0u, t.IndexCountPerInstance);
	g_IndirectArgs.Store(base + 4u, instanceCount);
	g_IndirectArgs.Store(base + 8u, t.StartIndexLocation);
	g_IndirectArgs.Store(base + 12u, t.BaseVertexLocation);
	g_IndirectArgs.Store(base + 16u, t.StartInstanceLocation);
}

[numthreads(64, 1, 1)]
void ResetMeshInstanceCounts(uint3 tid : SV_DispatchThreadID)
{
	uint meshId = tid.x;

	if (meshId >= MAX_NUM_INDIRECT_MESHES)
	{
		return;
	}

	g_MeshInstanceCountBuffer.Store(meshId * UINT_STRIDE_BYTES, 0u);
}
