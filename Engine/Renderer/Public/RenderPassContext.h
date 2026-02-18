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

#include "Engine/Renderer/Public/DrawPacket.h"

#include "Engine/Renderer/Public/PipelineStateManager.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	class RenderResourceRegistry;
	class AssetManager;

	struct RenderPassContext final
	{
		IRenderDevice* pDevice = nullptr;
		IDeviceContext* pImmediateContext = nullptr;
		ISwapChain* pSwapChain = nullptr;
		class Renderer* pRenderer = nullptr;
		IShaderSourceInputStreamFactory* pShaderSourceFactory = nullptr;
		AssetManager* pAssetManager = nullptr;
		PipelineStateManager* pPipelineStateManager = nullptr;
		RenderResourceRegistry* pRegistry = nullptr;
		RenderScene* pScene = nullptr;
		const ViewFamily* pViewFamily = nullptr;
		float DeltaTime = 0.0f;
		uint64 FrameIndex = 0;

		RefCntAutoPtr<IRenderPass> pRHIRenderPass;

		ViewFrustumExt MainViewFrustum;
		ViewFrustumExt ShadowViewFrustum;

		View ShadowView;

		// ------------------------------------------------------------
		// Per-pass packets (Renderer°¡ Ã¤¿ò)
		// ------------------------------------------------------------
		std::vector<DrawPacket> MainDrawPackets = {};
		std::vector<DrawPacket> ForwardDrawPackets = {};
		std::vector<DrawPacket> ShadowDrawPackets = {};
		std::vector<DrawPacket> DepthPrepassDrawPackets = {};

		std::vector<DrawIndirectPacket> MainIndirectPackets = {};
		std::vector<DrawIndirectPacket> ForwardIndirectPackets = {};
		std::vector<DrawIndirectPacket> ShadowIndirectPackets = {};
		std::vector<DrawIndirectPacket> DepthPrepassIndirectDrawPackets = {};

		void ResetFrame()
		{
			MainDrawPackets.clear();
			ForwardDrawPackets.clear();
			ShadowDrawPackets.clear();
			MainIndirectPackets.clear();
			ForwardIndirectPackets.clear();
			ShadowIndirectPackets.clear();
		}
	};
} // namespace shz
