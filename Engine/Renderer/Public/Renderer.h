// ============================================================
// Renderer.h (ONLY the necessary parts updated)
// - Fix texture slot design: Owner handle type != stored value type
// - Keep public GPU handle type: Handle<ITexture>
// - Store actual GPU object: RefCntAutoPtr<ITexture>
// ============================================================

#pragma once
#include <vector>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

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
#include "Engine/Renderer/Public/MaterialInstance.h"
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

        // AssetManager reference (not owned)
        AssetManager* pAssetManager = nullptr;

        uint32 BackBufferWidth = 0;
        uint32 BackBufferHeight = 0;

        // Hard-coded shader root in your current project; can be made configurable later
        const char* ShaderRootDir = "C:/Dev/ShizenEngine/Engine/Renderer/Shaders";
    };

    class Renderer final
    {
    public:
        Renderer() = default;
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        ~Renderer() = default;

        bool Initialize(const RendererCreateInfo& createInfo);
        void Cleanup();

        void OnResize(uint32 width, uint32 height);

        void BeginFrame();
        void Render(const RenderScene& scene, const ViewFamily& viewFamily);
        void EndFrame();

        Handle<StaticMeshRenderData> CreateStaticMesh(Handle<StaticMeshAsset> h);
        Handle<StaticMeshRenderData> CreateCubeMesh();

        bool DestroyStaticMesh(Handle<StaticMeshRenderData> h);
        bool DestroyMaterialInstance(Handle<MaterialInstance> h);
        bool DestroyTextureGPU(Handle<ITexture> h);

    private:
        // ------------------------------------------------------------
        // Internal storage policy: Slot + UniqueHandle
        // NOTE: For textures, handle type (ITexture) differs from stored value type (RefCntAutoPtr<ITexture>).
        // ------------------------------------------------------------
        template<typename T>
        struct Slot final
        {
            UniqueHandle<T> Owner = {};
            std::optional<T> Value = {};
        };

        // NEW: Slot that decouples handle type and stored value type
        template<typename HandleT, typename ValueT>
        struct SlotHV final
        {
            UniqueHandle<HandleT> Owner = {};
            std::optional<ValueT> Value = {};
        };

        template<typename TSlot>
        static void EnsureSlotCapacity(uint32 index, std::vector<TSlot>& slots)
        {
            if (index >= static_cast<uint32>(slots.size()))
            {
                slots.resize(static_cast<size_t>(index) + 1);
            }
        }

        static Slot<StaticMeshRenderData>* FindSlot(Handle<StaticMeshRenderData> h, std::vector<Slot<StaticMeshRenderData>>& slots) noexcept;
        static const Slot<StaticMeshRenderData>* FindSlot(Handle<StaticMeshRenderData> h, const std::vector<Slot<StaticMeshRenderData>>& slots) noexcept;

        static Slot<MaterialInstance>* FindSlot(Handle<MaterialInstance> h, std::vector<Slot<MaterialInstance>>& slots) noexcept;
        static const Slot<MaterialInstance>* FindSlot(Handle<MaterialInstance> h, const std::vector<Slot<MaterialInstance>>& slots) noexcept;

        // UPDATED: Texture slot uses SlotHV<ITexture, RefCntAutoPtr<ITexture>>
        static SlotHV<ITexture, RefCntAutoPtr<ITexture>>* FindTexSlot(Handle<ITexture> h, std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept;
        static const SlotHV<ITexture, RefCntAutoPtr<ITexture>>* FindTexSlot(Handle<ITexture> h, const std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept;

    private:
        Handle<ITexture> CreateTextureGPU(Handle<TextureAsset> h);
        Handle<MaterialInstance> CreateMaterialInstance(Handle<MaterialAsset> h);

        MaterialRenderData* GetOrCreateMaterialRenderData(Handle<MaterialInstance> h);
        bool CreateBasicPSO();

        const StaticMeshRenderData* TryGetMesh(Handle<StaticMeshRenderData> h) const noexcept;

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

        Handle<MaterialInstance> m_DefaultMaterial = {};
        RefCntAutoPtr<ISampler> m_pDefaultSampler;

        std::vector<Slot<StaticMeshRenderData>> m_MeshSlots;

        // TextureAsset -> GPU texture handle cache
        std::unordered_map<Handle<TextureAsset>, Handle<ITexture>> m_TexAssetToGpuHandle;

        // UPDATED: Texture slots are keyed by Handle<ITexture> but store RefCntAutoPtr<ITexture>
        std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>> m_TextureSlots;

        std::vector<Slot<MaterialInstance>> m_MaterialSlots;

        std::unordered_map<Handle<MaterialInstance>, MaterialRenderData> m_MatRenderDataTable;
    };
} // namespace shz
