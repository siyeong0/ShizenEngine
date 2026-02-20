#pragma once
#include <vector>
#include <array>

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
#include "Engine/Renderer/Public/ViewFamily.h"

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
		// Shadow cascades 
		// ------------------------------------------------------------
		static constexpr uint32 MAX_SHADOW_CASCADES = 8;

		std::array<View, MAX_SHADOW_CASCADES> ShadowCascadeViews = {};
		std::array<ViewFrustumExt, MAX_SHADOW_CASCADES> ShadowCascadeFrustums = {};

		std::array<std::vector<DrawPacket>, MAX_SHADOW_CASCADES> ShadowCascadeDrawPackets = {};
		std::array<std::vector<uint32>, MAX_SHADOW_CASCADES>     ShadowCascadeInstanceRemaps = {};

		std::vector<DrawIndirectPacket> ShadowIndirectPackets = {};

		// ------------------------------------------------------------
		// Per-pass packets
		// ------------------------------------------------------------
		std::vector<DrawPacket> MainDrawPackets = {};
		std::vector<DrawPacket> ForwardDrawPackets = {};
		std::vector<DrawPacket> DepthPrepassDrawPackets = {};

		std::vector<DrawIndirectPacket> MainIndirectPackets = {};
		std::vector<DrawIndirectPacket> ForwardIndirectPackets = {};
		std::vector<DrawIndirectPacket> DepthPrepassIndirectDrawPackets = {};

		void ResetFrame()
		{
			MainDrawPackets.clear();
			ForwardDrawPackets.clear();
			DepthPrepassDrawPackets.clear();

			MainIndirectPackets.clear();
			ForwardIndirectPackets.clear();
			DepthPrepassIndirectDrawPackets.clear();

			ShadowIndirectPackets.clear();

			for (uint32 i = 0; i < MAX_SHADOW_CASCADES; ++i)
			{
				ShadowCascadeDrawPackets[i].clear();
				ShadowCascadeInstanceRemaps[i].clear();
			}
		}
	};
} // namespace shz