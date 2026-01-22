#pragma once
#include <unordered_map>
#include <vector>
#include <string>

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
#include "Engine/RenderPass/Public/DrawPacket.h"

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

		// ---------------------------------------------------------------------
		// Visibility (computed by Renderer)
		// ---------------------------------------------------------------------
		std::vector<uint32> Visible_Main = {};
		std::vector<uint32> Visible_Shadow = {};

		// ---------------------------------------------------------------------
		// Passº° DrawPackets (computed by Renderer)
		//  - Key: pass name in m_PassOrder, e.g. "Shadow", "GBuffer", ...
		// ---------------------------------------------------------------------
		std::unordered_map<std::string, std::vector<DrawPacket>> DrawPacketsPerPass = {};

		// ---------------------------------------------------------------------
		// Common resources wired by Renderer
		// ---------------------------------------------------------------------
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
		// Per-frame caches (RD/barriers)
		// ---------------------------------------------------------------------
		std::vector<StateTransitionDesc> PreBarriers = {};
		std::unordered_map<uint64, Handle<MaterialRenderData>> FrameMat = {};
		std::vector<uint64> FrameMatKeys = {};

		void ResetFrame()
		{
			Visible_Main.clear();
			Visible_Shadow.clear();

			DrawPacketsPerPass.clear();

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

		std::vector<DrawPacket>& GetPassPackets(const std::string& passName)
		{
			auto it = DrawPacketsPerPass.find(passName);
			if (it == DrawPacketsPerPass.end())
			{
				DrawPacketsPerPass.insert(std::make_pair(passName, std::vector<DrawPacket>{}));
			}
			return DrawPacketsPerPass[passName];
		}
	};
} // namespace shz
