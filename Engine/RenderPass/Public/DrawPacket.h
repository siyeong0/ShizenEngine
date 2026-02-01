#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	enum EDrawCallType : uint8
	{
		Direct = 0,
		Indirect,
	};

	struct DrawPacket final
	{
		IBuffer* VertexBuffer = nullptr;
		IBuffer* IndexBuffer = nullptr;

		IPipelineState* PSO = nullptr;
		IShaderResourceBinding* SRB = nullptr;

		EDrawCallType DrawCallType = EDrawCallType::Direct;

		DrawIndexedAttribs DrawAttribs = {};
		DrawIndexedIndirectAttribs DrawIndirectAttribs = {};
	};
} // namespace shz