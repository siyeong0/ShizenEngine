#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	struct DrawPacket final
	{
		IBuffer* VertexBuffer = nullptr;
		IBuffer* IndexBuffer = nullptr;

		IPipelineState* PSO = nullptr;
		IShaderResourceBinding* SRB = nullptr;

		uint32 ObjectIndex = std::numeric_limits<uint32>::max();

		DrawIndexedAttribs DrawAttribs = {};
	};

    struct DrawPacketKey final
    {
        IPipelineState* PSO = nullptr;
        IShaderResourceBinding* SRB = nullptr;
        IBuffer* VB = nullptr;
        IBuffer* IB = nullptr;

        VALUE_TYPE IndexType = VT_UNDEFINED;

        uint32 NumIndices = 0;
        uint32 FirstIndexLocation = 0;
        int32  BaseVertex = 0;

        bool operator==(const DrawPacketKey& r) const noexcept
        {
            return PSO == r.PSO
                && SRB == r.SRB
                && VB == r.VB
                && IB == r.IB
                && IndexType == r.IndexType
                && NumIndices == r.NumIndices
                && FirstIndexLocation == r.FirstIndexLocation
                && BaseVertex == r.BaseVertex;
        }
    };

    struct DrawPacketKeyHasher final
    {
        size_t operator()(const DrawPacketKey& k) const noexcept
        {
            // pointer hashing + small ints
            size_t h = 1469598103934665603ull;
            auto mix = [&h](size_t v)
                {
                    h ^= v;
                    h *= 1099511628211ull;
                };

            mix(reinterpret_cast<size_t>(k.PSO));
            mix(reinterpret_cast<size_t>(k.SRB));
            mix(reinterpret_cast<size_t>(k.VB));
            mix(reinterpret_cast<size_t>(k.IB));

            mix(static_cast<size_t>(k.IndexType));
            mix(static_cast<size_t>(k.NumIndices));
            mix(static_cast<size_t>(k.FirstIndexLocation));
            mix(static_cast<size_t>(static_cast<uint32>(k.BaseVertex)));

            return h;
        }
    };

    struct BatchInfo final
    {
        DrawPacket Packet = {};          // template packet
        uint32 Count = 0;                // NumInstances
        uint32 FirstInstance = 0;         // FirstInstanceLocation (prefix sum result)
        uint32 Cursor = 0;               // write cursor inside this batch (Pass2)
    };
} // namespace shz