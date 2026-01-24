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

		bool operator==(const DrawPacketKey& rhs) const
		{
			return PSO == rhs.PSO
				&& SRB == rhs.SRB
				&& VB == rhs.VB
				&& IB == rhs.IB
				&& IndexType == rhs.IndexType
				&& NumIndices == rhs.NumIndices
				&& FirstIndexLocation == rhs.FirstIndexLocation
				&& BaseVertex == rhs.BaseVertex;
		}
	};

	struct DrawPacketKeyHasher final
	{
		size_t operator()(const DrawPacketKey& k) const noexcept
		{
			// pointer-heavy key: simple pointer hashing + mix
			auto hptr = [](const void* p) -> size_t
				{
					return std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(p));
				};

			size_t h = 1469598103934665603ull;
			auto mix = [&](size_t v)
				{
					h ^= v;
					h *= 1099511628211ull;
				};

			mix(hptr(k.PSO));
			mix(hptr(k.SRB));
			mix(hptr(k.VB));
			mix(hptr(k.IB));

			mix(std::hash<uint32>{}(static_cast<uint32>(k.IndexType)));
			mix(std::hash<uint32>{}(k.NumIndices));
			mix(std::hash<uint32>{}(k.FirstIndexLocation));
			mix(std::hash<int32>{}(k.BaseVertex));

			return h;
		}
	};

	struct DrawPacketBatch final
	{
		DrawPacket Packet = {};
		uint32 FirstInstanceLocation = 0;
		uint32 NumInstances = 0;
	};
}