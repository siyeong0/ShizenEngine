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
		float DeltaTime = 0.0f;
		uint32 ShadowMapResolution = 4096;

		RefCntAutoPtr<IRenderPass> pRHIRenderPass;

		// ------------------------------------------------------------
		// Per-pass packets (Renderer°¡ Ã¤¿ò)
		// ------------------------------------------------------------
		std::vector<DrawPacket> MainDrawPackets = {};
		std::vector<DrawPacket> ForwardDrawPackets = {};
		std::vector<DrawPacket> ShadowDrawPackets = {};

		void ResetFrame()
		{
			MainDrawPackets.clear();
			ShadowDrawPackets.clear();
		}
	};
} // namespace shz
