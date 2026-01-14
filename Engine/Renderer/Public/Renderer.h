#pragma once
#include <vector>
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/Renderer/Public/Handles.h"

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
#include "Engine/Renderer/Public/MaterialRenderData.h"
#include "Engine/Renderer/Public/ViewFamily.h"

// AssetManager forward
namespace shz { class AssetManager; }

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

        // NEW: AssetManager reference (not owned)
        AssetManager* pAssetManager = nullptr;

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

        // ----------------------------
        // Asset-driven creation
        // ----------------------------
        MeshHandle CreateStaticMesh(StaticMeshAssetHandle h);

        // Sample helper (still renderer-owned)
        MeshHandle CreateCubeMesh();

    private:
        // GPU cache creation helpers
        TextureHandle CreateTextureGPU(TextureAssetHandle h);
        MaterialHandle CreateMaterialInstance(MaterialAssetHandle h);

        MaterialRenderData* GetOrCreateMaterialRenderData(MaterialHandle h);
        bool CreateBasicPSO();

    private:
        RendererCreateInfo m_CreateInfo = {};
        AssetManager* m_pAssetManager = nullptr;

        uint32 m_Width = 0;
        uint32 m_Height = 0;

        RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

        RefCntAutoPtr<IBuffer> m_pFrameCB;
        RefCntAutoPtr<IBuffer> m_pObjectCB;
        RefCntAutoPtr<IPipelineState> m_pBasicPSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pBasicSRB;

        MaterialHandle m_DefaultMaterial = {};
        RefCntAutoPtr<ISampler> m_pDefaultSampler;

        // ----------------------------
        // GPU mesh table
        // ----------------------------
        uint32 m_NextMeshId = 1;
        std::unordered_map<MeshHandle, StaticMeshRenderData> m_MeshTable;

        // ----------------------------
        // GPU texture cache (by TextureAssetHandle)
        // ----------------------------
        uint32 m_NextTexId = 1;
        std::unordered_map<TextureAssetHandle, TextureHandle> m_TexAssetToGpuHandle;
        std::unordered_map<TextureHandle, RefCntAutoPtr<ITexture>> m_TextureTable;

        // ----------------------------
        // Runtime material instances (renderer-owned)
        // ----------------------------
        uint32 m_NextMaterialId = 1;
        std::unordered_map<MaterialHandle, MaterialInstance> m_MaterialTable;

        // Render data cache
        std::unordered_map<MaterialHandle, MaterialRenderData> m_MatRenderDataTable;
    };
} // namespace shz
