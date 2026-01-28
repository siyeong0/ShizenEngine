#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"

#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/ISwapChain.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

#include "Engine/RenderPass/Public/DrawPacket.h"

#include "Engine/Renderer/Public/PipelineStateManager.h"
#include "Engine/Renderer/Public/RenderData.h"

namespace shz
{
	class AssetManager;
	class IMaterialStaticBinder;

	struct RenderPassContext final
	{
		IRenderDevice* pDevice = nullptr;
		IDeviceContext* pImmediateContext = nullptr;
		ISwapChain* pSwapChain = nullptr;

		IShaderSourceInputStreamFactory* pShaderSourceFactory = nullptr;

		AssetManager* pAssetManager = nullptr;
		PipelineStateManager* pPipelineStateManager = nullptr;
		
		IMaterialStaticBinder* pGBufferMaterialStaticBinder = nullptr;
		IMaterialStaticBinder* pGrassMaterialStaticBinder = nullptr;
		IMaterialStaticBinder* pShadowMaterialStaticBinder = nullptr;

		// ------------------------------------------------------------
		// Visibility
		// ------------------------------------------------------------
		std::vector<uint32> VisibleObjectIndexMain = {};
		std::vector<uint32> VisibleObjectIndexShadow = {};

		// ------------------------------------------------------------
		// Per-pass packets (Renderer°¡ Ã¤¿ò)
		// ------------------------------------------------------------
		std::vector<DrawPacket> GBufferDrawPackets = {};
		std::vector<DrawPacket> GrassDrawPackets = {};

		std::vector<DrawPacket> ShadowDrawPackets = {};

		// ------------------------------------------------------------
		// Common resources wired by Renderer
		// ------------------------------------------------------------
		IBuffer* pFrameCB = nullptr;
		IBuffer* pDrawCB = nullptr;
		IBuffer* pShadowCB = nullptr;

		IBuffer* pObjectTableSBGBuffer = nullptr;
		IBuffer* pObjectTableSBGrass = nullptr;
		IBuffer* pObjectTableSBShadow = nullptr;
		IBuffer* pObjectIndexVB = nullptr;

		ITexture* pEnvTex = nullptr;
		ITexture* pEnvDiffuseTex = nullptr;
		ITexture* pEnvSpecularTex = nullptr;
		ITexture* pEnvBrdfTex = nullptr;

		uint32 BackBufferWidth = 0;
		uint32 BackBufferHeight = 0;

		// Pass outputs
		ITextureView* pLightingRtv = nullptr;
		ITextureView* pShadowMapSrv = nullptr;

		static constexpr uint32 NUM_GBUFFERS = 4;
		ITextureView* pGBufferSrv[NUM_GBUFFERS] = {};
		ITextureView* pDepthSrv = nullptr;

		ITextureView* pDepthDsv = nullptr;
		ITextureView* pLightingSrv = nullptr;

		const TextureRenderData* pHeightMap = nullptr;

		// ------------------------------------------------------------
		// Per-frame barrier list
		// ------------------------------------------------------------
		std::vector<StateTransitionDesc> PreBarriers = {};

		void ResetFrame()
		{
			VisibleObjectIndexMain.clear();
			VisibleObjectIndexShadow.clear();

			GBufferDrawPackets.clear();
			GrassDrawPackets.clear();
			ShadowDrawPackets.clear();

			PreBarriers.clear();
		}

		void PushBarrier(IDeviceObject* pObj, RESOURCE_STATE from, RESOURCE_STATE to)
		{
			ASSERT(pObj, "Device object is null.");

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
