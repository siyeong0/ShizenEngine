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
		//ASSERT(m_pLinearWrapSampler, "Linear wrap sampler is not set.");

		// FRAME_CONSTANTS is typically referenced in VS/PS.
		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
		{
			var->Set(m_pFrameCB);
		}

		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
		{
			var->Set(m_pFrameCB);
		}

		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "DRAW_CONSTANTS"))
		{
			var->Set(m_pDrawCB);
		}

		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "DRAW_CONSTANTS"))
		{
			var->Set(m_pDrawCB);
		}

		// Object indirection table (StructuredBuffer<ObjectConstants>).
		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
		{
			var->Set(m_pObjectTableSRV, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}

		if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_ObjectTable"))
		{
			var->Set(m_pObjectTableSRV, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}


		//// Typically referenced in PS; bind both stages just in case.
		//if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_LinearWrapSampler"))
		//	var->Set(m_pLinearWrapSampler);

		//if (auto* var = pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_LinearWrapSampler"))
		//	var->Set(m_pLinearWrapSampler);

		return true;
	}

} // namespace shz
