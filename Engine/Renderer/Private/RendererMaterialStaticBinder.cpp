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

		SHADER_TYPE supportedShaderTypes[] =
		{
			SHADER_TYPE_VERTEX,
			SHADER_TYPE_PIXEL,
			SHADER_TYPE_COMPUTE,
		};

		for (SHADER_TYPE type : supportedShaderTypes)
		{
			if (auto* var = pPSO->GetStaticVariableByName(type, "FRAME_CONSTANTS"))
			{
				var->Set(m_pFrameCB);
			}

			if (auto* var = pPSO->GetStaticVariableByName(type, "DRAW_CONSTANTS"))
			{
				var->Set(m_pDrawCB);
			}

			if (auto* var = pPSO->GetStaticVariableByName(type, "g_ObjectTable"))
			{
				var->Set(m_pObjectTableSRV, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}
		}

		return true;
	}

} // namespace shz
