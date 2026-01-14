#pragma once
#include <vector>
#include <unordered_map>

#include "Primitives/BasicTypes.h"

#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"

#include "Engine/RHI/Interface/IEngineFactory.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/ISwapChain.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/ImGui/Public/ImGuiImplShizen.hpp"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"
#include "Engine/Renderer/Public/ViewFamily.h"

namespace shz
{
	struct RendererCreateInfo
	{
		RefCntAutoPtr<IEngineFactory>  pEngineFactory;
		RefCntAutoPtr<IRenderDevice>   pDevice;
		RefCntAutoPtr<IDeviceContext>  pImmediateContext;
		std::vector<RefCntAutoPtr<IDeviceContext>> pDeferredContexts;
		RefCntAutoPtr<ISwapChain>      pSwapChain;
		ImGuiImplShizen* pImGui = nullptr;

		uint32 BackBufferWidth = 0;
		uint32 BackBufferHeight = 0;
	};

	class Renderer
	{
	public:
		bool Initialize(const RendererCreateInfo& createInfo);
		void Cleanup();

		void OnResize(uint32 width, uint32 height);

		void BeginFrame();
		void Render(const RenderScene& scene, const ViewFamily& viewFamily);
		void EndFrame();

		MeshHandle CreateStaticMesh(const StaticMeshAsset& asset);

		MeshHandle CreateCubeMesh();

	private:
		bool CreateBasicPSO();
		bool CreateCubeMesh_Internal(StaticMeshRenderData& outMesh);

	private:
		RendererCreateInfo m_CreateInfo = {};

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		RefCntAutoPtr<IBuffer> m_pFrameCB;
		RefCntAutoPtr<IBuffer> m_pObjectCB;
		RefCntAutoPtr<IPipelineState> m_pBasicPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pBasicSRB;

		uint32 m_NextMeshId = 1;
		std::unordered_map<MeshHandle, StaticMeshRenderData> m_MeshTable;
	};
} // namespace shz
