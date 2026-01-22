#pragma once
#include <unordered_map>
#include <vector>

#include "Primitives/BasicTypes.h"

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/ISwapChain.h"
#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

#include "Primitives/Handle.hpp"
#include "Engine/Renderer/Public/MaterialRenderData.h"

namespace shz
{
	class AssetManager;
	class RenderResourceCache;
	class RendererMaterialStaticBinder;

	struct RenderPassContext final
	{
		IRenderDevice* pDevice = nullptr;
		IDeviceContext* pImmediateContext = nullptr;
		ISwapChain* pSwapChain = nullptr;

		IShaderSourceInputStreamFactory* pShaderSourceFactory = nullptr;

		AssetManager* pAssetManager = nullptr;
		RenderResourceCache* pCache = nullptr;
		RendererMaterialStaticBinder* pMaterialStaticBinder = nullptr;

		uint32 VisibleObjectCount = 0;
		std::vector<uint32> VisibleObjectIndices = {};

		IBuffer* pFrameCB = nullptr;
		IBuffer* pShadowCB = nullptr;

		IBuffer* pObjectTableSB = nullptr;
		IBuffer* pObjectIndexVB = nullptr;

		ITexture* pEnvTex = nullptr;
		ITexture* pEnvDiffuseTex = nullptr;
		ITexture* pEnvSpecularTex = nullptr;
		ITexture* pEnvBrdfTex = nullptr;

		uint32 BackBufferWidth = 0;
		uint32 BackBufferHeight = 0;

		// Pass outputs (wired by Renderer::wirePassOutputs)
		ITextureView* pShadowMapSrv = nullptr;

		static constexpr uint32 NUM_GBUFFERS = 4;
		ITextureView* pGBufferSrv[NUM_GBUFFERS] = {};
		ITextureView* pDepthSrv = nullptr;

		ITextureView* pLightingSrv = nullptr;

		// ---------------------------------------------------------------------
		// Per-frame caches (moved from old Renderer members)
		// ---------------------------------------------------------------------
		std::vector<StateTransitionDesc> PreBarriers = {};
		std::unordered_map<uint64, Handle<MaterialRenderData>> FrameMat = {};
		std::vector<uint64> FrameMatKeys = {};

		void ResetPerFrameCaches()
		{
			PreBarriers.clear();
			FrameMat.clear();
			FrameMatKeys.clear();
		}

		void PushBarrier(IDeviceObject* pObj, RESOURCE_STATE from, RESOURCE_STATE to)
		{
			if (!pObj)
				return;

			StateTransitionDesc b = {};
			b.pResource = pObj;
			b.OldState = from;
			b.NewState = to;
			b.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;
			PreBarriers.push_back(b);
		}

		void UploadObjectIndexInstance(uint32 objectIndex) const
		{
			ASSERT(pImmediateContext, "Context is null.");
			ASSERT(pObjectIndexVB, "ObjectIndex VB is null.");

			MapHelper<uint32> map(pImmediateContext, pObjectIndexVB, MAP_WRITE, MAP_FLAG_DISCARD);
			*map = objectIndex;
		}
	};
} // namespace shz
