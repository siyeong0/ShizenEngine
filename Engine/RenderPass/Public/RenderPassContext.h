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

namespace shz
{
	class AssetManager;
	class RenderResourceCache;
	class PipelineStateManager;
	class RendererMaterialStaticBinder;

    struct RenderPassContext final
    {
        IRenderDevice* pDevice = nullptr;
        IDeviceContext* pImmediateContext = nullptr;
        ISwapChain* pSwapChain = nullptr;

        IShaderSourceInputStreamFactory* pShaderSourceFactory = nullptr;

        AssetManager* pAssetManager = nullptr;
        RenderResourceCache* pCache = nullptr;
        PipelineStateManager* pPipelineStateManager = nullptr;
        RendererMaterialStaticBinder* pMaterialStaticBinder = nullptr;

        // ------------------------------------------------------------
        // Visibility
        // ------------------------------------------------------------
        std::vector<uint32> VisibleObjectIndexMain = {};
        std::vector<uint32> VisibleObjectIndexShadow = {};

        // ------------------------------------------------------------
        // Per-pass packets (Renderer°¡ Ã¤¿ò)
        // ------------------------------------------------------------
        std::vector<DrawPacket> GBufferDrawPackets = {};
        std::vector<DrawPacket> ShadowDrawPackets = {};

        // ------------------------------------------------------------
        // Common resources wired by Renderer
        // ------------------------------------------------------------
        IBuffer* pFrameCB = nullptr;
        IBuffer* pDrawCB = nullptr;
        IBuffer* pShadowCB = nullptr;

        IBuffer* pObjectTableSB = nullptr;
        IBuffer* pObjectTableSBShadow = nullptr;
        IBuffer* pObjectIndexVB = nullptr;

        ITexture* pEnvTex = nullptr;
        ITexture* pEnvDiffuseTex = nullptr;
        ITexture* pEnvSpecularTex = nullptr;
        ITexture* pEnvBrdfTex = nullptr;

        uint32 BackBufferWidth = 0;
        uint32 BackBufferHeight = 0;

        // Pass outputs
        ITextureView* pShadowMapSrv = nullptr;

        static constexpr uint32 NUM_GBUFFERS = 4;
        ITextureView* pGBufferSrv[NUM_GBUFFERS] = {};
        ITextureView* pDepthSrv = nullptr;

        ITextureView* pLightingSrv = nullptr;

        // ------------------------------------------------------------
        // Per-frame barrier list
        // ------------------------------------------------------------
        std::vector<StateTransitionDesc> PreBarriers = {};

        void ResetFrame()
        {
            VisibleObjectIndexMain.clear();
            VisibleObjectIndexShadow.clear();

            GBufferDrawPackets.clear();
            ShadowDrawPackets.clear();

            PreBarriers.clear();
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
