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
		// Geometry
		IBuffer* VertexBuffer = nullptr;
		IBuffer* IndexBuffer = nullptr;

		uint32 ObjectIndex = std::numeric_limits<uint32>::max();

		DrawIndexedAttribs DrawAttribs = {};

		// Material binding
		IPipelineState* PSO = nullptr;
		IShaderResourceBinding* SRB = nullptr;

		// Sorting keys (optional cache)
		uint64 SortKey0 = 0;
		uint64 SortKey1 = 0;
	};
}