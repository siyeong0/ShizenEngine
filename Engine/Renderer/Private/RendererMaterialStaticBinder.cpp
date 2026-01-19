#include "pch.h"
#include "Engine/Renderer/Public/RendererMaterialStaticBinder.h"
#include "Engine/RHI/Interface/IShaderResourceVariable.h"

namespace shz
{
	bool RendererMaterialStaticBinder::BindStatics(IPipelineState* pPSO)
	{
		ASSERT(pPSO, "PSO is null.");
		ASSERT(m_pFrameCB, "Frame constant buffer is not set.");
		ASSERT(m_pObjectTableSRV, "Object table srv is not set.");

		// FRAME_CONSTANTS is typically referenced in VS/PS.
		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
		{
			var->Set(m_pFrameCB);
		}

		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
		{
			var->Set(m_pFrameCB);
		}

		// Object indirection table (StructuredBuffer<ObjectConstants>).
		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
		{
			var->Set(m_pObjectTableSRV);
		}

		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_ObjectTable"))
		{
			var->Set(m_pObjectTableSRV);
		}

		return true;
	}

} // namespace shz
