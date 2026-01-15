#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Tools/Image/Public/TextureUtilities.h"

namespace shz
{
    namespace hlsl
    {
#include "Shaders/HLSL_Structures.hlsli"
    } // namespace hlsl

    // ------------------------------------------------------------
    // Slot helpers (StaticMeshRenderData)
    // ------------------------------------------------------------

    Renderer::Slot<StaticMeshRenderData>* Renderer::FindSlot(Handle<StaticMeshRenderData> h, std::vector<Slot<StaticMeshRenderData>>& slots) noexcept
    {
        if (!h.IsValid())
            return nullptr;

        if (!Handle<StaticMeshRenderData>::IsAlive(h))
            return nullptr;

        const uint32 index = h.GetIndex();
        if (index == 0 || index >= static_cast<uint32>(slots.size()))
            return nullptr;

        auto& slot = slots[index];
        if (!slot.Owner.Get().IsValid())
            return nullptr;

        if (!slot.Value.has_value())
            return nullptr;

        return &slot;
    }

    const Renderer::Slot<StaticMeshRenderData>* Renderer::FindSlot(Handle<StaticMeshRenderData> h, const std::vector<Slot<StaticMeshRenderData>>& slots) noexcept
    {
        if (!h.IsValid())
            return nullptr;

        if (!Handle<StaticMeshRenderData>::IsAlive(h))
            return nullptr;

        const uint32 index = h.GetIndex();
        if (index == 0 || index >= static_cast<uint32>(slots.size()))
            return nullptr;

        const auto& slot = slots[index];
        if (!slot.Owner.Get().IsValid())
            return nullptr;

        if (!slot.Value.has_value())
            return nullptr;

        return &slot;
    }

    // ------------------------------------------------------------
    // Slot helpers (MaterialInstance)
    // ------------------------------------------------------------

    Renderer::Slot<MaterialInstance>* Renderer::FindSlot(Handle<MaterialInstance> h, std::vector<Slot<MaterialInstance>>& slots) noexcept
    {
        if (!h.IsValid())
            return nullptr;

        if (!Handle<MaterialInstance>::IsAlive(h))
            return nullptr;

        const uint32 index = h.GetIndex();
        if (index == 0 || index >= static_cast<uint32>(slots.size()))
            return nullptr;

        auto& slot = slots[index];
        if (!slot.Owner.Get().IsValid())
            return nullptr;

        if (!slot.Value.has_value())
            return nullptr;

        return &slot;
    }

    const Renderer::Slot<MaterialInstance>* Renderer::FindSlot(Handle<MaterialInstance> h, const std::vector<Slot<MaterialInstance>>& slots) noexcept
    {
        if (!h.IsValid())
            return nullptr;

        if (!Handle<MaterialInstance>::IsAlive(h))
            return nullptr;

        const uint32 index = h.GetIndex();
        if (index == 0 || index >= static_cast<uint32>(slots.size()))
            return nullptr;

        const auto& slot = slots[index];
        if (!slot.Owner.Get().IsValid())
            return nullptr;

        if (!slot.Value.has_value())
            return nullptr;

        return &slot;
    }

    // ------------------------------------------------------------
    // Slot helpers (Texture)  [FIXED]
    // ------------------------------------------------------------

    Renderer::SlotHV<ITexture, RefCntAutoPtr<ITexture>>* Renderer::FindTexSlot(
        Handle<ITexture> h,
        std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept
    {
        if (!h.IsValid())
            return nullptr;

        if (!Handle<ITexture>::IsAlive(h))
            return nullptr;

        const uint32 index = h.GetIndex();
        if (index == 0 || index >= static_cast<uint32>(slots.size()))
            return nullptr;

        auto& slot = slots[index];
        if (!slot.Owner.Get().IsValid())
            return nullptr;

        if (!slot.Value.has_value())
            return nullptr;

        return &slot;
    }

    const Renderer::SlotHV<ITexture, RefCntAutoPtr<ITexture>>* Renderer::FindTexSlot(
        Handle<ITexture> h,
        const std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept
    {
        if (!h.IsValid())
            return nullptr;

        if (!Handle<ITexture>::IsAlive(h))
            return nullptr;

        const uint32 index = h.GetIndex();
        if (index == 0 || index >= static_cast<uint32>(slots.size()))
            return nullptr;

        const auto& slot = slots[index];
        if (!slot.Owner.Get().IsValid())
            return nullptr;

        if (!slot.Value.has_value())
            return nullptr;

        return &slot;
    }

    // ------------------------------------------------------------
    // TryGetMesh
    // ------------------------------------------------------------

    const StaticMeshRenderData* Renderer::TryGetMesh(Handle<StaticMeshRenderData> h) const noexcept
    {
        const auto* slot = FindSlot(h, m_MeshSlots);
        if (!slot)
            return nullptr;

        return &slot->Value.value();
    }

    // ============================================================
    // Renderer
    // ============================================================

    bool Renderer::Initialize(const RendererCreateInfo& createInfo)
    {
        m_CreateInfo = createInfo;
        m_pAssetManager = m_CreateInfo.pAssetManager;

        if (!m_CreateInfo.pDevice || !m_CreateInfo.pImmediateContext || !m_CreateInfo.pSwapChain || !m_pAssetManager)
            return false;

        const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();
        m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : SCDesc.Width;
        m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : SCDesc.Height;

        // Shader source factory
        m_CreateInfo.pEngineFactory->CreateDefaultShaderSourceStreamFactory(
            m_CreateInfo.ShaderRootDir,
            &m_pShaderSourceFactory);

        CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Frame constants CB", &m_pFrameCB);
        CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ObjectConstants), "Object constants CB", &m_pObjectCB);

        if (!CreateBasicPSO())
            return false;

        // Default sampler
        {
            SamplerDesc SamDesc = {};
            SamDesc.MinFilter = FILTER_TYPE_LINEAR;
            SamDesc.MagFilter = FILTER_TYPE_LINEAR;
            SamDesc.MipFilter = FILTER_TYPE_LINEAR;
            SamDesc.AddressU = TEXTURE_ADDRESS_WRAP;
            SamDesc.AddressV = TEXTURE_ADDRESS_WRAP;
            SamDesc.AddressW = TEXTURE_ADDRESS_WRAP;

            m_CreateInfo.pDevice->CreateSampler(SamDesc, &m_pDefaultSampler);
            ASSERT(m_pDefaultSampler, "Failed to create default sampler.");
        }

        // Default material instance (renderer-owned)
        {
            MaterialInstance mat = {};
            mat.OverrideBaseColorFactor(float3(1.0f, 1.0f, 1.0f));
            mat.OverrideOpacity(1.0f);
            mat.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE);
            mat.OverrideRoughness(0.5f);
            mat.OverrideMetallic(0.0f);

            UniqueHandle<MaterialInstance> owner = UniqueHandle<MaterialInstance>::Make();
            const Handle<MaterialInstance> h = owner.Get();
            const uint32 index = h.GetIndex();

            EnsureSlotCapacity(index, m_MaterialSlots);

            auto& slot = m_MaterialSlots[index];
            ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "Default material slot already occupied.");

            slot.Owner = std::move(owner);
            slot.Value.emplace(std::move(mat));

            m_DefaultMaterial = h;
        }

        return true;
    }

    void Renderer::Cleanup()
    {
        m_pBasicSRB.Release();
        m_pBasicPSO.Release();

        m_pFrameCB.Release();
        m_pObjectCB.Release();

        m_pShaderSourceFactory.Release();
        m_pDefaultSampler.Release();

        for (auto& s : m_MeshSlots)
        {
            s.Value.reset();
            s.Owner.Reset();
        }

        for (auto& s : m_TextureSlots)
        {
            s.Value.reset();
            s.Owner.Reset();
        }

        for (auto& s : m_MaterialSlots)
        {
            s.Value.reset();
            s.Owner.Reset();
        }

        m_MeshSlots.clear();
        m_TextureSlots.clear();
        m_MaterialSlots.clear();

        m_TexAssetToGpuHandle.clear();
        m_MatRenderDataTable.clear();

        m_DefaultMaterial = {};
        m_pAssetManager = nullptr;

        m_CreateInfo = {};
        m_Width = 0;
        m_Height = 0;
    }

    void Renderer::OnResize(uint32 width, uint32 height)
    {
        m_Width = width;
        m_Height = height;
    }

    void Renderer::BeginFrame() {}
    void Renderer::EndFrame() {}

    // ============================================================
    // Explicit destroy helpers
    // ============================================================

    bool Renderer::DestroyStaticMesh(Handle<StaticMeshRenderData> h)
    {
        auto* slot = FindSlot(h, m_MeshSlots);
        if (!slot)
            return false;

        slot->Value.reset();
        slot->Owner.Reset();
        return true;
    }

    bool Renderer::DestroyMaterialInstance(Handle<MaterialInstance> h)
    {
        auto* slot = FindSlot(h, m_MaterialSlots);
        if (!slot)
            return false;

        if (auto it = m_MatRenderDataTable.find(h); it != m_MatRenderDataTable.end())
            m_MatRenderDataTable.erase(it);

        slot->Value.reset();
        slot->Owner.Reset();
        return true;
    }

    bool Renderer::DestroyTextureGPU(Handle<ITexture> h)
    {
        auto* slot = FindTexSlot(h, m_TextureSlots);
        if (!slot)
            return false;

        slot->Value.reset();
        slot->Owner.Reset();
        return true;
    }

    // ============================================================
    // GPU resource creation (from AssetManager-owned CPU assets)
    // ============================================================

    Handle<ITexture> Renderer::CreateTextureGPU(Handle<TextureAsset> h)
    {
        if (!h.IsValid())
            return {};

        // Cache hit
        if (auto it = m_TexAssetToGpuHandle.find(h); it != m_TexAssetToGpuHandle.end())
        {
            const Handle<ITexture> cached = it->second;

            if (Handle<ITexture>::IsAlive(cached))
            {
                const auto* slot = FindTexSlot(cached, m_TextureSlots);
                if (slot)
                    return cached;
            }

            // stale map
            m_TexAssetToGpuHandle.erase(it);
        }

        const TextureAsset& texAsset = m_pAssetManager->GetTexture(h);
        TextureLoadInfo loadInfo = texAsset.BuildTextureLoadInfo();

        RefCntAutoPtr<ITexture> pTex;
        CreateTextureFromFile(texAsset.GetSourcePath().c_str(), loadInfo, m_CreateInfo.pDevice, &pTex);
        if (!pTex)
            return {};

        if (ITextureView* pSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
        {
            if (m_pDefaultSampler)
                pSRV->SetSampler(m_pDefaultSampler);
        }

        // FIXED: public handle is Handle<ITexture>, stored object is RefCntAutoPtr<ITexture>
        UniqueHandle<ITexture> owner = UniqueHandle<ITexture>::Make();
        const Handle<ITexture> gpuHandle = owner.Get();
        const uint32 index = gpuHandle.GetIndex();

        EnsureSlotCapacity(index, m_TextureSlots);

        auto& slot = m_TextureSlots[index];
        ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "Texture slot already occupied.");

        slot.Owner = std::move(owner);
        slot.Value.emplace(pTex);

        m_TexAssetToGpuHandle.emplace(h, gpuHandle);
        return gpuHandle;
    }

    Handle<MaterialInstance> Renderer::CreateMaterialInstance(Handle<MaterialAsset> h)
    {
        if (!h.IsValid())
            return m_DefaultMaterial;

        const MaterialAsset& matAsset = m_pAssetManager->GetMaterial(h);

        MaterialInstance inst = {};

        // Parameters
        const auto& P = matAsset.GetParams();

        inst.OverrideBaseColorFactor(float3(P.BaseColor.x, P.BaseColor.y, P.BaseColor.z));
        inst.OverrideOpacity(P.BaseColor.w);

        inst.OverrideMetallic(P.Metallic);
        inst.OverrideRoughness(P.Roughness);

        inst.OverrideNormalScale(P.NormalScale);
        inst.OverrideOcclusionStrength(P.Occlusion);

        inst.OverrideEmissiveFactor(P.EmissiveColor * P.EmissiveIntensity);
        inst.OverrideAlphaCutoff(P.AlphaCutoff);

        // Alpha mode mapping
        switch (matAsset.GetOptions().AlphaMode)
        {
        case MATERIAL_ALPHA_OPAQUE: inst.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE); break;
        case MATERIAL_ALPHA_MASK:   inst.OverrideAlphaMode(MATERIAL_ALPHA_MASK);   break;
        case MATERIAL_ALPHA_BLEND:  inst.OverrideAlphaMode(MATERIAL_ALPHA_BLEND);  break;
        default:                    inst.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE); break;
        }

        // Textures (MaterialAsset stores TextureAsset by value currently)
        if (matAsset.HasTexture(MATERIAL_TEX_ALBEDO))
        {
            Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_ALBEDO));
            inst.OverrideBaseColorTexture(hTex);
        }

        if (matAsset.HasTexture(MATERIAL_TEX_NORMAL))
        {
            Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_NORMAL));
            inst.OverrideNormalTexture(hTex);
        }

        if (matAsset.HasTexture(MATERIAL_TEX_ORM))
        {
            Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_ORM));
            inst.OverrideMetallicRoughnessTexture(hTex);
        }

        if (matAsset.HasTexture(MATERIAL_TEX_EMISSIVE))
        {
            Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_EMISSIVE));
            inst.OverrideEmissiveTexture(hTex);
        }

        UniqueHandle<MaterialInstance> owner = UniqueHandle<MaterialInstance>::Make();
        const Handle<MaterialInstance> hInst = owner.Get();
        const uint32 index = hInst.GetIndex();

        EnsureSlotCapacity(index, m_MaterialSlots);

        auto& slot = m_MaterialSlots[index];
        ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "MaterialInstance slot already occupied.");

        slot.Owner = std::move(owner);
        slot.Value.emplace(std::move(inst));

        return hInst;
    }

    Handle<StaticMeshRenderData> Renderer::CreateStaticMesh(Handle<StaticMeshAsset> h)
    {
        if (!h.IsValid())
            return {};

        const StaticMeshAsset& meshAsset = m_pAssetManager->GetStaticMesh(h);

        UniqueHandle<StaticMeshRenderData> owner = UniqueHandle<StaticMeshRenderData>::Make();
        const Handle<StaticMeshRenderData> handle = owner.Get();
        const uint32 index = handle.GetIndex();

        EnsureSlotCapacity(index, m_MeshSlots);

        auto& slot = m_MeshSlots[index];
        ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "StaticMeshRenderData slot already occupied.");

        StaticMeshRenderData outMesh = {};

        struct SimpleVertex
        {
            float3 Pos;
            float2 UV;
            float3 Normal;
            float3 Tangent;
        };

        const uint32 vtxCount = meshAsset.GetVertexCount();
        outMesh.NumVertices = vtxCount;
        outMesh.VertexStride = sizeof(SimpleVertex);
        outMesh.LocalBounds = meshAsset.GetBounds();

        const auto& positions = meshAsset.GetPositions();
        const auto& normals = meshAsset.GetNormals();
        const auto& tangents = meshAsset.GetTangents();
        const auto& uvs = meshAsset.GetTexCoords();

        std::vector<SimpleVertex> vbCPU;
        vbCPU.resize(vtxCount);

        for (uint32 i = 0; i < vtxCount; ++i)
        {
            SimpleVertex v = {};
            v.Pos = positions[i];
            v.UV = uvs[i];
            v.Normal = normals[i];
            v.Tangent = tangents[i];
            vbCPU[i] = v;
        }

        // VB
        {
            BufferDesc VBDesc = {};
            VBDesc.Name = "StaticMesh VB";
            VBDesc.Usage = USAGE_IMMUTABLE;
            VBDesc.BindFlags = BIND_VERTEX_BUFFER;
            VBDesc.Size = static_cast<uint64>(vbCPU.size() * sizeof(SimpleVertex));

            BufferData VBData = {};
            VBData.pData = vbCPU.data();
            VBData.DataSize = static_cast<uint64>(vbCPU.size() * sizeof(SimpleVertex));

            m_CreateInfo.pDevice->CreateBuffer(VBDesc, &VBData, &outMesh.VertexBuffer);
            if (!outMesh.VertexBuffer)
                return {};
        }

        outMesh.Sections.clear();

        const bool useU32 = (meshAsset.GetIndexType() == VT_UINT32);
        const auto& idx32 = meshAsset.GetIndicesU32();
        const auto& idx16 = meshAsset.GetIndicesU16();

        auto CreateSectionIB = [&](MeshSection& sec, const void* pIndexData, uint64 indexDataBytes) -> bool
        {
            if (!pIndexData || indexDataBytes == 0)
                return false;

            BufferDesc IBDesc = {};
            IBDesc.Name = "StaticMesh IB";
            IBDesc.Usage = USAGE_IMMUTABLE;
            IBDesc.BindFlags = BIND_INDEX_BUFFER;
            IBDesc.Size = indexDataBytes;

            BufferData IBData = {};
            IBData.pData = pIndexData;
            IBData.DataSize = indexDataBytes;

            m_CreateInfo.pDevice->CreateBuffer(IBDesc, &IBData, &sec.IndexBuffer);
            return (sec.IndexBuffer != nullptr);
        };

        const auto& assetSections = meshAsset.GetSections();
        if (!assetSections.empty())
        {
            outMesh.Sections.reserve(assetSections.size());

            for (const StaticMeshAsset::Section& asec : assetSections)
            {
                if (asec.IndexCount == 0)
                    continue;

                MeshSection sec = {};
                sec.NumIndices = asec.IndexCount;
                sec.IndexType = useU32 ? VT_UINT32 : VT_UINT16;
                sec.LocalBounds = asec.LocalBounds;
                sec.StartIndex = 0;

                const uint32 first = asec.FirstIndex;
                const uint32 count = asec.IndexCount;

                const void* pData = nullptr;
                uint64 bytes = 0;

                if (useU32)
                {
                    pData = (idx32.empty()) ? nullptr : static_cast<const void*>(idx32.data() + first);
                    bytes = static_cast<uint64>(count) * sizeof(uint32);
                }
                else
                {
                    pData = (idx16.empty()) ? nullptr : static_cast<const void*>(idx16.data() + first);
                    bytes = static_cast<uint64>(count) * sizeof(uint16);
                }

                if (!CreateSectionIB(sec, pData, bytes))
                    return {};

                const MaterialAsset& slotMat = meshAsset.GetMaterialSlot(asec.MaterialSlot);
                Handle<MaterialAsset> hMatAsset = m_pAssetManager->RegisterMaterial(slotMat);
                sec.Material = CreateMaterialInstance(hMatAsset);

                outMesh.Sections.push_back(sec);
            }
        }
        else
        {
            MeshSection sec = {};
            sec.NumIndices = meshAsset.GetIndexCount();
            sec.IndexType = useU32 ? VT_UINT32 : VT_UINT16;
            sec.StartIndex = 0;

            if (!CreateSectionIB(sec, meshAsset.GetIndexData(), meshAsset.GetIndexDataSizeBytes()))
                return {};

            sec.Material = m_DefaultMaterial;
            outMesh.Sections.push_back(sec);
        }

        slot.Owner = std::move(owner);
        slot.Value.emplace(std::move(outMesh));
        return handle;
    }

    // ============================================================
    // Render
    // ============================================================

    void Renderer::Render(const RenderScene& scene, const ViewFamily& viewFamily)
    {
        auto* pCtx = m_CreateInfo.pImmediateContext.RawPtr();
        auto* pSC = m_CreateInfo.pSwapChain.RawPtr();
        if (!pCtx || !pSC)
            return;

        ITextureView* pRTV = pSC->GetCurrentBackBufferRTV();
        ITextureView* pDSV = pSC->GetDepthBufferDSV();
        pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const float ClearColor[] = { 0.350f, 0.350f, 0.350f, 1.0f };
        pCtx->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
        pCtx->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

        if (viewFamily.Views.empty())
            return;

        const auto& view = viewFamily.Views[0];
        Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;

        // Frame CB update
        {
            MapHelper<hlsl::FrameConstants> CBData(pCtx, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);
            CBData->ViewProj = viewProj;
        }

        if (!m_pBasicPSO)
            return;

        for (const auto& h : scene.GetObjectHandles())
        {
            const RenderScene::RenderObject* renderObject = scene.TryGetObject(h);

            const StaticMeshRenderData* pMesh = TryGetMesh(renderObject->MeshHandle);
            if (!pMesh)
                continue;

            // Object CB update
            {
                MapHelper<hlsl::ObjectConstants> CBData(pCtx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
                CBData->World = renderObject->Transform;
            }

            IBuffer* pVBs[] = { pMesh->VertexBuffer };
            uint64 Offsets[] = { 0 };
            pCtx->SetVertexBuffers(
                0, 1, pVBs, Offsets,
                RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                SET_VERTEX_BUFFERS_FLAG_RESET);

            Handle<MaterialInstance> lastMat = {};

            for (const auto& section : pMesh->Sections)
            {
                pCtx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                const Handle<MaterialInstance> matHandle = section.Material;
                MaterialRenderData* matRD = GetOrCreateMaterialRenderData(matHandle);
                if (!matRD)
                    continue;

                if (matHandle != lastMat)
                {
                    pCtx->SetPipelineState(matRD->pPSO);
                    pCtx->CommitShaderResources(matRD->pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    lastMat = matHandle;
                }

                DrawIndexedAttribs DIA = {};
                DIA.NumIndices = section.NumIndices;
                DIA.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
                DIA.Flags = DRAW_FLAG_VERIFY_ALL;
                DIA.FirstIndexLocation = section.StartIndex;

                pCtx->DrawIndexed(DIA);
            }
        }
    }

    // ============================================================
    // MaterialRenderData (SRB/PSO bindings)
    // ============================================================

    MaterialRenderData* Renderer::GetOrCreateMaterialRenderData(Handle<MaterialInstance> h)
    {
        if (!h.IsValid())
            return nullptr;

        const auto* instSlot = FindSlot(h, m_MaterialSlots);
        if (!instSlot)
            return nullptr;

        // cache hit
        if (auto it = m_MatRenderDataTable.find(h); it != m_MatRenderDataTable.end())
            return &it->second;

        const MaterialInstance& MatInst = instSlot->Value.value();

        if (!m_pBasicPSO)
            return nullptr;

        // Fallback white SRV (create once)
        static RefCntAutoPtr<ITextureView> s_WhiteSRV;
        if (!s_WhiteSRV)
        {
            auto* pDevice = m_CreateInfo.pDevice.RawPtr();
            if (!pDevice)
                return nullptr;

            const uint32 whiteRGBA = 0xFFFFFFFFu;

            TextureDesc TexDesc = {};
            TexDesc.Name = "DefaultWhite1x1";
            TexDesc.Type = RESOURCE_DIM_TEX_2D;
            TexDesc.Width = 1;
            TexDesc.Height = 1;
            TexDesc.MipLevels = 1;
            TexDesc.Format = TEX_FORMAT_RGBA8_UNORM;
            TexDesc.Usage = USAGE_IMMUTABLE;
            TexDesc.BindFlags = BIND_SHADER_RESOURCE;

            TextureSubResData Sub = {};
            Sub.pData = &whiteRGBA;
            Sub.Stride = sizeof(uint32);

            TextureData InitData = {};
            InitData.pSubResources = &Sub;
            InitData.NumSubresources = 1;

            RefCntAutoPtr<ITexture> pWhiteTex;
            pDevice->CreateTexture(TexDesc, &InitData, &pWhiteTex);
            if (!pWhiteTex)
                return nullptr;

            s_WhiteSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            if (!s_WhiteSRV)
                return nullptr;

            if (m_pDefaultSampler)
                s_WhiteSRV->SetSampler(m_pDefaultSampler);
        }

        // Resolve effective params
        const float3 BaseColor = MatInst.GetBaseColorFactor(float3(1, 1, 1));
        const float  Opacity = MatInst.GetOpacity(1.0f);
        const float  Metallic = MatInst.GetMetallic(0.0f);
        const float  Roughness = MatInst.GetRoughness(0.5f);
        const float  NormalScale = MatInst.GetNormalScale(1.0f);
        const float  Occlusion = MatInst.GetOcclusionStrength(1.0f);
        const float3 Emissive = MatInst.GetEmissiveFactor(float3(0, 0, 0));
        const auto   AlphaMode = MatInst.GetAlphaMode(MATERIAL_ALPHA_OPAQUE);
        const float  AlphaCutoff = MatInst.GetAlphaCutoff(0.5f);

        MaterialRenderData RD = {};
        RD.InstanceHandle = h; // If your struct uses InstanceHandle, change this line.

        RD.RenderQueue = MaterialRenderData::GetQueueFromAlphaMode(AlphaMode);

        RD.BaseColor = float4(BaseColor.x, BaseColor.y, BaseColor.z, Opacity);
        RD.Metallic = Metallic;
        RD.Roughness = Roughness;
        RD.NormalScale = NormalScale;
        RD.OcclusionStrength = Occlusion;
        RD.Emissive = Emissive;
        RD.AlphaCutoff = AlphaCutoff;

        RD.pPSO = m_pBasicPSO;

        RD.pPSO->CreateShaderResourceBinding(&RD.pSRB, true);
        if (!RD.pSRB)
            return nullptr;

        // Bind VS constant buffers
        auto BindVS_CB = [&](const char* name, IBuffer* pCB) -> bool
        {
            if (!pCB) return false;
            if (auto* Var = RD.pSRB->GetVariableByName(SHADER_TYPE_VERTEX, name))
            {
                Var->Set(pCB);
                return true;
            }
            return false;
        };

        if (!BindVS_CB("FRAME_CONSTANTS", m_pFrameCB))
            return nullptr;

        if (!BindVS_CB("OBJECT_CONSTANTS", m_pObjectCB))
            return nullptr;

        // Base color SRV
        ITextureView* pBaseColorSRV = s_WhiteSRV.RawPtr();

        const Handle<TextureAsset> baseColorAssetHandle = MatInst.GetBaseColorTextureOverride();
        if (baseColorAssetHandle.IsValid())
        {
            const Handle<ITexture> texGPUHandle = CreateTextureGPU(baseColorAssetHandle);
            if (texGPUHandle.IsValid())
            {
                const auto* texSlot = FindTexSlot(texGPUHandle, m_TextureSlots);
                if (texSlot && texSlot->Value.has_value())
                {
                    const RefCntAutoPtr<ITexture>& pTex = texSlot->Value.value();
                    if (pTex)
                    {
                        if (auto* pSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
                        {
                            if (m_pDefaultSampler)
                                pSRV->SetSampler(m_pDefaultSampler);
                            pBaseColorSRV = pSRV;
                        }
                    }
                }
            }
        }

        if (auto* TexVar = RD.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
        {
            TexVar->Set(pBaseColorSRV);
        }
        else
        {
            ASSERT(false, "PS SRB variable 'g_BaseColorTex' not found.");
            return nullptr;
        }

        auto [insIt, ok] = m_MatRenderDataTable.emplace(h, std::move(RD));
        (void)ok;
        return &insIt->second;
    }

    // ============================================================
    // PSO
    // ============================================================

    bool Renderer::CreateBasicPSO()
    {
        auto* pDevice = m_CreateInfo.pDevice.RawPtr();
        if (!pDevice)
            return false;

        const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();

        GraphicsPipelineStateCreateInfo PSOCreateInfo = {};
        PSOCreateInfo.PSODesc.Name = "Debug Basic PSO";
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        auto& GP = PSOCreateInfo.GraphicsPipeline;
        GP.NumRenderTargets = 1;
        GP.RTVFormats[0] = SCDesc.ColorBufferFormat;
        GP.DSVFormat = SCDesc.DepthBufferFormat;
        GP.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        GP.RasterizerDesc.CullMode = CULL_MODE_NONE;
        GP.RasterizerDesc.FrontCounterClockwise = true;
        GP.DepthStencilDesc.DepthEnable = true;

        LayoutElement LayoutElems[] =
        {
            LayoutElement{0, 0, 3, VT_FLOAT32, false},
            LayoutElement{1, 0, 2, VT_FLOAT32, false},
            LayoutElement{2, 0, 3, VT_FLOAT32, false},
            LayoutElement{3, 0, 3, VT_FLOAT32, false},
        };
        GP.InputLayout.LayoutElements = LayoutElems;
        GP.InputLayout.NumElements = _countof(LayoutElems);

        ShaderCreateInfo ShaderCI = {};
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.EntryPoint = "main";
        ShaderCI.pShaderSourceStreamFactory = m_pShaderSourceFactory;
        ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc = {};
            ShaderCI.Desc.Name = "Basic VS";
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.FilePath = "Basic.vsh";
            ShaderCI.Desc.UseCombinedTextureSamplers = true;

            pDevice->CreateShader(ShaderCI, &pVS);
            if (!pVS)
                return false;
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc = {};
            ShaderCI.Desc.Name = "Basic PS";
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.FilePath = "Basic.psh";
            ShaderCI.Desc.UseCombinedTextureSamplers = true;

            pDevice->CreateShader(ShaderCI, &pPS);
            if (!pPS)
                return false;
        }

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

        ShaderResourceVariableDesc Vars[] =
        {
            { SHADER_TYPE_VERTEX, "FRAME_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
            { SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC  },
            { SHADER_TYPE_PIXEL,  "g_BaseColorTex",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
        };
        PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
        PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

        pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pBasicPSO);
        if (!m_pBasicPSO)
            return false;

        m_pBasicPSO->CreateShaderResourceBinding(&m_pBasicSRB, true);
        if (!m_pBasicSRB)
            return false;

        auto BindCB = [&](const char* name, IBuffer* pCB) -> bool
        {
            if (!pCB) return false;
            if (auto* Var = m_pBasicSRB->GetVariableByName(SHADER_TYPE_VERTEX, name))
            {
                Var->Set(pCB);
                return true;
            }
            return false;
        };

        if (!BindCB("FRAME_CONSTANTS", m_pFrameCB))
            return false;

        if (!BindCB("OBJECT_CONSTANTS", m_pObjectCB))
            return false;

        return true;
    }

    // ============================================================
    // Cube mesh
    // ============================================================

    Handle<StaticMeshRenderData> Renderer::CreateCubeMesh()
    {
        UniqueHandle<StaticMeshRenderData> owner = UniqueHandle<StaticMeshRenderData>::Make();
        const Handle<StaticMeshRenderData> handle = owner.Get();
        const uint32 index = handle.GetIndex();

        EnsureSlotCapacity(index, m_MeshSlots);

        auto& slot = m_MeshSlots[index];
        ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "Cube mesh slot already occupied.");

        StaticMeshRenderData outMesh = {};

        struct SimpleVertex
        {
            float3 Pos;
            float2 UV;
            float3 Normal;
            float3 Tangent;
        };

        static const SimpleVertex Verts[] =
        {
            {{-0.5f,-0.5f,-0.5f},{0,1},{0,0,-1},{+1,0,0}},
            {{+0.5f,-0.5f,-0.5f},{1,1},{0,0,-1},{+1,0,0}},
            {{+0.5f,+0.5f,-0.5f},{1,0},{0,0,-1},{+1,0,0}},
            {{-0.5f,+0.5f,-0.5f},{0,0},{0,0,-1},{+1,0,0}},

            {{-0.5f,-0.5f,+0.5f},{0,1},{0,0,+1},{+1,0,0}},
            {{+0.5f,-0.5f,+0.5f},{1,1},{0,0,+1},{+1,0,0}},
            {{+0.5f,+0.5f,+0.5f},{1,0},{0,0,+1},{+1,0,0}},
            {{-0.5f,+0.5f,+0.5f},{0,0},{0,0,+1},{+1,0,0}},

            {{-0.5f,-0.5f,+0.5f},{0,1},{-1,0,0},{0,0,-1}},
            {{-0.5f,-0.5f,-0.5f},{1,1},{-1,0,0},{0,0,-1}},
            {{-0.5f,+0.5f,-0.5f},{1,0},{-1,0,0},{0,0,-1}},
            {{-0.5f,+0.5f,+0.5f},{0,0},{-1,0,0},{0,0,-1}},

            {{+0.5f,-0.5f,-0.5f},{0,1},{+1,0,0},{0,0,+1}},
            {{+0.5f,-0.5f,+0.5f},{1,1},{+1,0,0},{0,0,+1}},
            {{+0.5f,+0.5f,+0.5f},{1,0},{+1,0,0},{0,0,+1}},
            {{+0.5f,+0.5f,-0.5f},{0,0},{+1,0,0},{0,0,+1}},

            {{-0.5f,-0.5f,+0.5f},{0,1},{0,-1,0},{+1,0,0}},
            {{+0.5f,-0.5f,+0.5f},{1,1},{0,-1,0},{+1,0,0}},
            {{+0.5f,-0.5f,-0.5f},{1,0},{0,-1,0},{+1,0,0}},
            {{-0.5f,-0.5f,-0.5f},{0,0},{0,-1,0},{+1,0,0}},

            {{-0.5f,+0.5f,-0.5f},{0,1},{0,+1,0},{+1,0,0}},
            {{+0.5f,+0.5f,-0.5f},{1,1},{0,+1,0},{+1,0,0}},
            {{+0.5f,+0.5f,+0.5f},{1,0},{0,+1,0},{+1,0,0}},
            {{-0.5f,+0.5f,+0.5f},{0,0},{0,+1,0},{+1,0,0}},
        };

        static const uint32 Indices[] =
        {
            0,2,1, 0,3,2,
            4,5,6, 4,6,7,
            8,10,9, 8,11,10,
            12,14,13, 12,15,14,
            16,18,17, 16,19,18,
            20,22,21, 20,23,22
        };

        outMesh.NumVertices = _countof(Verts);
        outMesh.VertexStride = sizeof(SimpleVertex);
        outMesh.LocalBounds = { {-0.5f,-0.5f,-0.5f}, {+0.5f,+0.5f,+0.5f} };

        // VB
        {
            BufferDesc VBDesc = {};
            VBDesc.Name = "Cube VB";
            VBDesc.Usage = USAGE_IMMUTABLE;
            VBDesc.BindFlags = BIND_VERTEX_BUFFER;
            VBDesc.Size = sizeof(Verts);

            BufferData VBData = {};
            VBData.pData = Verts;
            VBData.DataSize = sizeof(Verts);

            m_CreateInfo.pDevice->CreateBuffer(VBDesc, &VBData, &outMesh.VertexBuffer);
            if (!outMesh.VertexBuffer)
                return {};
        }

        // IB (one section)
        MeshSection sec = {};
        sec.NumIndices = _countof(Indices);
        sec.IndexType = VT_UINT32;
        sec.StartIndex = 0;

        {
            BufferDesc IBDesc = {};
            IBDesc.Name = "Cube IB";
            IBDesc.Usage = USAGE_IMMUTABLE;
            IBDesc.BindFlags = BIND_INDEX_BUFFER;
            IBDesc.Size = sizeof(Indices);

            BufferData IBData = {};
            IBData.pData = Indices;
            IBData.DataSize = sizeof(Indices);

            m_CreateInfo.pDevice->CreateBuffer(IBDesc, &IBData, &sec.IndexBuffer);
            if (!sec.IndexBuffer)
                return {};
        }

        sec.Material = m_DefaultMaterial;

        outMesh.Sections.clear();
        outMesh.Sections.push_back(sec);

        slot.Owner = std::move(owner);
        slot.Value.emplace(std::move(outMesh));

        return handle;
    }

} // namespace shz
