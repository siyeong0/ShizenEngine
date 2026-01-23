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
}