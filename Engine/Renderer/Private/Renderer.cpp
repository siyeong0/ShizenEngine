#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include <unordered_set>

#include "Engine/Core/Math/Math.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

#include "Tools/Image/Public/TextureUtilities.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"

#include "Engine/RenderPass/Public/ShadowRenderPass.h"
#include "Engine/RenderPass/Public/GBufferRenderPass.h"
#include "Engine/RenderPass/Public/LightingRenderPass.h"
#include "Engine/RenderPass/Public/PostRenderPass.h"

#include "Engine/RenderPass/Public/DrawPacket.h"

#include <chrono>
#include <iostream>


namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		ASSERT(createInfo.pDevice, "Device is null.");
		ASSERT(createInfo.pImmediateContext, "ImmediateContext is null.");
		ASSERT(createInfo.pSwapChain, "SwapChain is null.");
		ASSERT(createInfo.pAssetManager, "AssetManager is null.");
		ASSERT(createInfo.pShaderSourceFactory, "ShaderSourceFactory is null.");

		m_CreateInfo = createInfo;
		m_pAssetManager = createInfo.pAssetManager;
		m_pShaderSourceFactory = createInfo.pShaderSourceFactory;

		const SwapChainDesc& scDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : scDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : scDesc.Height;

		m_pPipelineStateManager = std::make_unique<PipelineStateManager>();
		m_pPipelineStateManager->Initialize(m_CreateInfo.pDevice);

		m_pCache = std::make_unique<RenderResourceCache>();
		m_pCache->Initialize(m_CreateInfo.pDevice.RawPtr(), m_pAssetManager);
		m_pCache->SetErrorTexture("C:/Dev/ShizenEngine/Assets/Error.jpg");

		m_pMaterialStaticBinder = std::make_unique<RendererMaterialStaticBinder>();

		// Create shared buffers
		{
			ASSERT(m_CreateInfo.pDevice, "Device is null.");
			ASSERT(m_CreateInfo.pImmediateContext, "Context is null.");

			IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();

			CreateUniformBuffer(dev, sizeof(hlsl::FrameConstants), "Frame constants", &m_pFrameCB);
			CreateUniformBuffer(dev, sizeof(hlsl::DrawConstants), "Draw constants", &m_pDrawCB);
			CreateUniformBuffer(dev, sizeof(hlsl::ShadowConstants), "Shadow constants", &m_pShadowCB);

			ASSERT(m_pFrameCB, "Frame CB create failed.");
			ASSERT(m_pDrawCB, "Frame CB create failed.");
			ASSERT(m_pShadowCB, "Shadow CB create failed.");

			// Create object index instance buffer
			{
				IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
				ASSERT(dev, "Device is null.");

				ASSERT(m_pObjectIndexVB == nullptr, "m_pObjectIndexVB is already initilaized");

				BufferDesc desc = {};
				desc.Name = "ObjectIndexInstanceVB";
				desc.Usage = USAGE_DYNAMIC;
				desc.BindFlags = BIND_VERTEX_BUFFER;
				desc.CPUAccessFlags = CPU_ACCESS_WRITE;
				desc.Size = sizeof(uint32);

				dev->CreateBuffer(desc, nullptr, &m_pObjectIndexVB);
				ASSERT(m_pObjectIndexVB, "Object index VB create failed.");
			}

			// Allocate object table
			{
				IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
				ASSERT(dev, "Device is null.");

				BufferDesc desc = {};
				desc.Name = "ObjectTableSB";
				desc.Usage = USAGE_DYNAMIC;
				desc.BindFlags = BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = CPU_ACCESS_WRITE;
				desc.Mode = BUFFER_MODE_STRUCTURED;
				desc.ElementByteStride = sizeof(hlsl::ObjectConstants);
				desc.Size = uint64(desc.ElementByteStride) * uint64(DEFAULT_MAX_OBJECT_COUNT);

				RefCntAutoPtr<IBuffer> newBuf;
				dev->CreateBuffer(desc, nullptr, &newBuf);
				ASSERT(newBuf, "Object table create failed.");

				m_pObjectTableSB = newBuf;
				m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();

				if (m_pMaterialStaticBinder)
				{
					m_pMaterialStaticBinder->SetObjectTableSRV(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
				}
			}

			{
				IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
				ASSERT(dev, "Device is null.");

				BufferDesc desc = {};
				desc.Name = "ObjectTableSBShadow";
				desc.Usage = USAGE_DYNAMIC;
				desc.BindFlags = BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = CPU_ACCESS_WRITE;
				desc.Mode = BUFFER_MODE_STRUCTURED;
				desc.ElementByteStride = sizeof(hlsl::ObjectConstants);
				desc.Size = uint64(desc.ElementByteStride) * uint64(DEFAULT_MAX_OBJECT_COUNT);

				RefCntAutoPtr<IBuffer> newBuf;
				dev->CreateBuffer(desc, nullptr, &newBuf);
				ASSERT(newBuf, "Object table create failed.");

				m_pObjectTableSBShadow = newBuf;
				m_PassCtx.pObjectTableSBShadow = m_pObjectTableSBShadow.RawPtr();
			}
		}

		// Create env textures
		{
			TextureLoadInfo tli = {};
			/*CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskyEnvHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskyDiffuseHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvDiffuseTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskySpecularHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvSpecularTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskyBrdf.dds", tli, m_CreateInfo.pDevice, &m_EnvBrdfTex);*/

			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleEnvHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleDiffuseHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvDiffuseTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleSpecularHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvSpecularTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleBrdf.dds", tli, m_CreateInfo.pDevice, &m_EnvBrdfTex);


			ASSERT(m_EnvTex, "Env tex load failed.");
			ASSERT(m_EnvDiffuseTex, "Env diffuse load failed.");
			ASSERT(m_EnvSpecularTex, "Env specular load failed.");
			ASSERT(m_EnvBrdfTex, "Env brdf load failed.");
		}

		m_pMaterialStaticBinder->SetFrameConstants(m_pFrameCB);
		m_pMaterialStaticBinder->SetDrawConstants(m_pDrawCB);
		m_pMaterialStaticBinder->SetObjectTableSRV(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

		m_PassCtx = {};
		m_PassCtx.pDevice = m_CreateInfo.pDevice.RawPtr();
		m_PassCtx.pImmediateContext = m_CreateInfo.pImmediateContext.RawPtr();
		m_PassCtx.pSwapChain = m_CreateInfo.pSwapChain.RawPtr();
		m_PassCtx.pShaderSourceFactory = m_pShaderSourceFactory.RawPtr();
		m_PassCtx.pAssetManager = m_pAssetManager;
		m_PassCtx.pCache = m_pCache.get();
		m_PassCtx.pPipelineStateManager = m_pPipelineStateManager.get();
		m_PassCtx.pMaterialStaticBinder = m_pMaterialStaticBinder.get();

		m_PassCtx.pFrameCB = m_pFrameCB.RawPtr();
		m_PassCtx.pDrawCB = m_pDrawCB.RawPtr();
		m_PassCtx.pShadowCB = m_pShadowCB.RawPtr();
		m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();
		m_PassCtx.pObjectTableSBShadow = m_pObjectTableSBShadow.RawPtr();
		m_PassCtx.pObjectIndexVB = m_pObjectIndexVB.RawPtr();

		m_PassCtx.pEnvTex = m_EnvTex.RawPtr();
		m_PassCtx.pEnvDiffuseTex = m_EnvDiffuseTex.RawPtr();
		m_PassCtx.pEnvSpecularTex = m_EnvSpecularTex.RawPtr();
		m_PassCtx.pEnvBrdfTex = m_EnvBrdfTex.RawPtr();

		m_PassCtx.BackBufferWidth = m_Width;
		m_PassCtx.BackBufferHeight = m_Height;

		// Create render passes
		{
			ASSERT(m_Passes.empty(), "m_Passes are already initilaized.");
			ASSERT(m_PassOrder.empty(), "m_PassOrder are already initilaized.");
			addPass(std::make_unique<ShadowRenderPass>());
			addPass(std::make_unique<GBufferRenderPass>());
			addPass(std::make_unique<LightingRenderPass>());
			addPass(std::make_unique<PostRenderPass>());

			for (const std::string& name : m_PassOrder)
			{
				RenderPassBase* pass = m_Passes[name].get();
				ASSERT(pass, "Pass is null.");

				const bool ok = pass->Initialize(m_PassCtx);
				ASSERT(ok, "Pass initialize failed.");
			}
		}

		wirePassOutputs();

		return true;
	}

	void Renderer::Cleanup()
	{
		ReleaseSwapChainBuffers();

		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->Cleanup();
		}

		m_Passes.clear();
		m_PassOrder.clear();

		m_EnvTex.Release();
		m_EnvDiffuseTex.Release();
		m_EnvSpecularTex.Release();
		m_EnvBrdfTex.Release();

		m_pObjectIndexVB.Release();
		m_pObjectTableSB.Release();
		m_pObjectTableSBShadow.Release();

		m_pShadowCB.Release();
		m_pFrameCB.Release();

		if (m_pCache)
		{
			m_pCache->Shutdown();
			m_pCache.reset();
		}

		m_pMaterialStaticBinder.reset();

		m_pShaderSourceFactory.Release();
		m_pAssetManager = nullptr;
		m_pPipelineStateManager->Clear();
		m_pPipelineStateManager.reset();

		m_CreateInfo = {};
		m_PassCtx = {};
		m_Width = 0;
		m_Height = 0;
	}

	void Renderer::BeginFrame()
	{
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->BeginFrame(m_PassCtx);
		}
	}

    void Renderer::Render(RenderScene& scene, const ViewFamily& viewFamily)
    {
        using Clock = std::chrono::high_resolution_clock;
        const auto tBegin = Clock::now();

        ASSERT(m_PassCtx.pImmediateContext, "Context is invalid.");
        ASSERT(!viewFamily.Views.empty(), "No view.");
        ASSERT(m_PassCtx.pCache, "Cache is null.");
        ASSERT(m_PassCtx.pMaterialStaticBinder, "MaterialStaticBinder is null.");

        IDeviceContext* ctx = m_PassCtx.pImmediateContext;
        m_PassCtx.ResetFrame();

        // Ensure SBs exist
        ASSERT(m_pObjectTableSB, "ObjectTableSB (GBuffer) is null.");
        ASSERT(m_pObjectTableSBShadow, "ObjectTableSBShadow is null.");
        ASSERT(m_PassCtx.pDrawCB, "DrawCB is null.");

        // Wire SB pointers
        m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();
        m_PassCtx.pObjectTableSBShadow = m_pObjectTableSBShadow.RawPtr();

        const View& view = viewFamily.Views[0];

        // ------------------------------------------------------------
        // Update Frame/Shadow constants + compute lightViewProj
        // ------------------------------------------------------------
        Matrix4x4 lightViewProj = {};
        {
            MapHelper<hlsl::FrameConstants> cb(ctx, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);

            cb->View = view.ViewMatrix;
            cb->Proj = view.ProjMatrix;
            cb->ViewProj = view.ViewMatrix * view.ProjMatrix;
            cb->InvViewProj = cb->ViewProj.Inversed();

            cb->CameraPosition = view.CameraPosition;

            cb->ViewportSize =
            {
                static_cast<float>(view.Viewport.right - view.Viewport.left),
                static_cast<float>(view.Viewport.bottom - view.Viewport.top)
            };

            cb->InvViewportSize =
            {
                1.f / cb->ViewportSize.x,
                1.f / cb->ViewportSize.y
            };

            cb->NearPlane = view.NearPlane;
            cb->FarPlane = view.FarPlane;
            cb->DeltaTime = viewFamily.DeltaTime;
            cb->CurrTime = viewFamily.CurrentTime;

            // Global light (first one)
            const RenderScene::LightObject* globalLight = nullptr;
            for (const auto& l : scene.GetLights())
            {
                globalLight = &l;
                break;
            }

            float3 lightDirWs = globalLight ? globalLight->Direction.Normalized() : float3(0, -1, 0);
            float3 lightColor = globalLight ? globalLight->Color : float3(1, 1, 1);
            float  lightIntensity = globalLight ? globalLight->Intensity : 1.0f;

            const float3 lightForward = lightDirWs;
            const float3 centerWs = view.CameraPosition;

            const float shadowDistance = 50.0f;
            const float3 lightPosWs = centerWs - lightForward * shadowDistance;

            float3 up = float3(0, 1, 0);
            if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
            {
                up = float3(0, 0, 1);
            }

            const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);

            const float radiusXY = 25.0f;
            const float nearZ = 0.1f;
            const float farZ = 200.0f;
            const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(
                -radiusXY, +radiusXY,
                -radiusXY, +radiusXY,
                nearZ, farZ);

            lightViewProj = lightView * lightProj;

            cb->LightViewProj = lightViewProj;
            cb->LightDirWS = lightDirWs;
            cb->LightColor = lightColor;
            cb->LightIntensity = lightIntensity;
        }

        {
            MapHelper<hlsl::ShadowConstants> cb(ctx, m_pShadowCB, MAP_WRITE, MAP_FLAG_DISCARD);
            cb->LightViewProj = lightViewProj;
        }

        // ------------------------------------------------------------
        // Build frustums: Main / Shadow (main/shadow 분리)
        // ------------------------------------------------------------
        ViewFrustumExt frMain = {};
        ViewFrustumExt frShadow = {};
        {
            const Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;
            ExtractViewFrustumPlanesFromMatrix(viewProj, frMain);
            ExtractViewFrustumPlanesFromMatrix(lightViewProj, frShadow);
        }

        // ------------------------------------------------------------
        // Visibility (Mesh 단위, Section culling 없음)
        //  - Shadow는 Main과 독립 (Main에 안 보여도 Shadow에 보이면 포함)
        // ------------------------------------------------------------
        {
            const auto& renderObjects = scene.GetObjects();
            const uint32 count = static_cast<uint32>(renderObjects.size());

            m_PassCtx.VisibleObjectIndexMain.reserve(count);
            m_PassCtx.VisibleObjectIndexShadow.reserve(count);

            for (uint32 i = 0; i < count; ++i)
            {
                const RenderScene::RenderObject& obj = renderObjects[i];

                const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
                ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

                const Box& localBounds = mesh->GetLocalBounds();

                if (IntersectsFrustum(frMain, localBounds, obj.Transform, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
                {
                    m_PassCtx.VisibleObjectIndexMain.push_back(i);
                }

                if (obj.bCastShadow)
                {
                    if (IntersectsFrustum(frShadow, localBounds, obj.Transform, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
                    {
                        m_PassCtx.VisibleObjectIndexShadow.push_back(i);
                    }
                }
            }
        }

        // ------------------------------------------------------------
        // Common barriers (Frame/Shadow/Env/etc.)
        // ------------------------------------------------------------
        m_PassCtx.PushBarrier(m_pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
        m_PassCtx.PushBarrier(m_pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

        m_PassCtx.PushBarrier(m_pObjectTableSB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
        m_PassCtx.PushBarrier(m_pObjectTableSBShadow, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

        m_PassCtx.PushBarrier(m_PassCtx.pDrawCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

        m_PassCtx.PushBarrier(m_EnvTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
        m_PassCtx.PushBarrier(m_EnvDiffuseTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
        m_PassCtx.PushBarrier(m_EnvSpecularTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
        m_PassCtx.PushBarrier(m_EnvBrdfTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

        if (m_PassCtx.pCache)
        {
            m_PassCtx.PushBarrier(m_PassCtx.pCache->GetErrorTexture().GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
        }

        auto& objectsMutable = scene.GetObjects();

        // ------------------------------------------------------------
        // Material RD apply (이번 프레임에 "사용될" RD는 전부 Apply)
        //  - MainVisible의 모든 섹션 RD
        //  - ShadowVisible 중 alpha-masked 섹션 RD (MaterialConstants out-of-date 버그의 핵심)
        //  - 중복 Apply 방지
        // ------------------------------------------------------------
        std::unordered_set<MaterialRenderData*> appliedRD;
        appliedRD.reserve(1024);

        auto applyMaterialIfNeeded = [&](MaterialRenderData* rd)
            {
                if (!rd)
                    return;

                if (appliedRD.find(rd) != appliedRD.end())
                    return;

                appliedRD.insert(rd);

                // Dynamic MaterialConstants 포함한 SRB 업데이트를 여기서 보장
                rd->Apply(m_PassCtx.pCache, ctx);

                // Barriers: mat CB / bound textures
                if (auto* matCB = rd->GetMaterialConstantsBuffer())
                {
                    m_PassCtx.PushBarrier(matCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
                }

                for (auto texHandle : rd->GetBoundTextures())
                {
                    auto texRD = m_PassCtx.pCache->TryGetTextureRenderData(texHandle);
                    ASSERT(texRD && texRD->IsValid(), "Texture is invalid.");
                    m_PassCtx.PushBarrier(texRD->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
                }
            };

        // Mesh VB/IB barriers + material apply (Main)
        for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
        {
            RenderScene::RenderObject& obj = objectsMutable[objectIndex];

            const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
            ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

            m_PassCtx.PushBarrier(mesh->GetVertexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
            m_PassCtx.PushBarrier(mesh->GetIndexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

            for (const auto& sec : mesh->GetSections())
            {
                const uint32 slot = sec.MaterialSlot;
                ASSERT(slot < static_cast<uint32>(obj.Materials.size()), "Material slot index out of bounds.");

                Handle<MaterialRenderData> hRD = obj.Materials[slot];
                ASSERT(hRD.IsValid(), "Material in object is not valid.");

                MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(hRD);
                ASSERT(rd, "Material render data is null.");

                applyMaterialIfNeeded(rd);
            }
        }

        // Shadow: alpha-masked only (opaque shadow SRB는 보통 material constants 필요 없음)
        for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexShadow)
        {
            RenderScene::RenderObject& obj = objectsMutable[objectIndex];
            if (!obj.bCastShadow || !obj.bAlphaMasked)
                continue;

            const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
            ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

            m_PassCtx.PushBarrier(mesh->GetVertexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
            m_PassCtx.PushBarrier(mesh->GetIndexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

            for (const auto& sec : mesh->GetSections())
            {
                const uint32 slot = sec.MaterialSlot;
                ASSERT(slot < static_cast<uint32>(obj.Materials.size()), "Material slot index out of bounds.");

                Handle<MaterialRenderData> hRD = obj.Materials[slot];
                ASSERT(hRD.IsValid(), "Material in object is not valid.");

                MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(hRD);
                ASSERT(rd, "Material render data is null.");

                applyMaterialIfNeeded(rd);
            }
        }

        // Apply transitions once
        if (!m_PassCtx.PreBarriers.empty())
        {
            ctx->TransitionResourceStates(static_cast<uint32>(m_PassCtx.PreBarriers.size()), m_PassCtx.PreBarriers.data());
        }

        // ============================================================
        //  Build GBuffer packets + pack ObjectTableSB (2-pass batching)
        // ============================================================
        {
            std::unordered_map<DrawPacketKey, BatchInfo, DrawPacketKeyHasher> batches;
            batches.reserve(m_PassCtx.VisibleObjectIndexMain.size() * 4);

            // -------- Pass 1: count instances per batch
            uint32 totalInstances = 0;

            for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
            {
                RenderScene::RenderObject& obj = objectsMutable[objectIndex];

                const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
                ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

                IBuffer* vb = mesh->GetVertexBuffer();
                IBuffer* ib = mesh->GetIndexBuffer();
                ASSERT(vb && ib, "Mesh buffers are invalid.");

                const VALUE_TYPE indexType = mesh->GetIndexType();

                for (const auto& sec : mesh->GetSections())
                {
                    ASSERT(sec.IndexCount > 0, "Invalid section. IndexCount == 0.");

                    const uint32 slot = sec.MaterialSlot;
                    ASSERT(slot < static_cast<uint32>(obj.Materials.size()), "Material slot index out of bounds.");

                    Handle<MaterialRenderData> hRD = obj.Materials[slot];
                    ASSERT(hRD.IsValid(), "Material instance in object is null.");

                    MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(hRD);
                    ASSERT(rd && rd->GetPSO() && rd->GetSRB(), "Material render data is invalid.");

                    DrawPacketKey key = {};
                    key.PSO = rd->GetPSO();
                    key.SRB = rd->GetSRB();
                    key.VB = vb;
                    key.IB = ib;
                    key.IndexType = indexType;
                    key.NumIndices = sec.IndexCount;
                    key.FirstIndexLocation = sec.FirstIndex;
                    key.BaseVertex = static_cast<int32>(sec.BaseVertex);

                    auto it = batches.find(key);
                    if (it == batches.end())
                    {
                        BatchInfo bi = {};
                        bi.Packet = {};
                        bi.Packet.VertexBuffer = vb;
                        bi.Packet.IndexBuffer = ib;
                        bi.Packet.PSO = key.PSO;
                        bi.Packet.SRB = key.SRB;
                        bi.Packet.ObjectIndex = 0; // instance table path

                        bi.Packet.DrawAttribs = {};
                        bi.Packet.DrawAttribs.NumIndices = key.NumIndices;
                        bi.Packet.DrawAttribs.FirstIndexLocation = key.FirstIndexLocation;
                        bi.Packet.DrawAttribs.BaseVertex = key.BaseVertex;
                        bi.Packet.DrawAttribs.IndexType = key.IndexType;
                        bi.Packet.DrawAttribs.NumInstances = 0;              // finalize later
                        bi.Packet.DrawAttribs.FirstInstanceLocation = 0;     // finalize later
                        bi.Packet.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

                        auto [newIt, ok] = batches.emplace(key, bi);
                        ASSERT(ok, "Failed to emplace GBuffer batch.");
                        it = newIt;
                    }

                    it->second.Count += 1;
                    totalInstances += 1;
                }
            }

            ASSERT(totalInstances < DEFAULT_MAX_OBJECT_COUNT, "Increase object table capacity (GBuffer).");

            // -------- Prefix sum: assign FirstInstance per batch
            {
                uint32 cursor = 0;
                for (auto& kv : batches)
                {
                    BatchInfo& bi = kv.second;
                    bi.FirstInstance = cursor;
                    bi.Cursor = 0;
                    cursor += bi.Count;
                }
            }

            // -------- Pass 2: fill SB at the correct contiguous ranges
            {
                MapHelper<hlsl::ObjectConstants> map(ctx, m_pObjectTableSB, MAP_WRITE, MAP_FLAG_DISCARD);
                hlsl::ObjectConstants* dst = map;

                for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
                {
                    const RenderScene::RenderObject& obj = objectsMutable[objectIndex];

                    const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
                    ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

                    IBuffer* vb = mesh->GetVertexBuffer();
                    IBuffer* ib = mesh->GetIndexBuffer();
                    const VALUE_TYPE indexType = mesh->GetIndexType();

                    for (const auto& sec : mesh->GetSections())
                    {
                        const uint32 slot = sec.MaterialSlot;
                        Handle<MaterialRenderData> hRD = obj.Materials[slot];

                        MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(hRD);

                        DrawPacketKey key = {};
                        key.PSO = rd->GetPSO();
                        key.SRB = rd->GetSRB();
                        key.VB = vb;
                        key.IB = ib;
                        key.IndexType = indexType;
                        key.NumIndices = sec.IndexCount;
                        key.FirstIndexLocation = sec.FirstIndex;
                        key.BaseVertex = static_cast<int32>(sec.BaseVertex);

                        auto it = batches.find(key);
                        ASSERT(it != batches.end(), "Batch not found (GBuffer).");

                        BatchInfo& bi = it->second;

                        const uint32 writeIndex = bi.FirstInstance + bi.Cursor;
                        bi.Cursor += 1;

                        hlsl::ObjectConstants oc = {};
                        oc.World = obj.Transform;
                        oc.WorldInvTranspose = obj.Transform.Inversed().Transposed();

                        dst[writeIndex] = oc;
                    }
                }
            }

            // -------- Finalize packets
            m_PassCtx.GBufferDrawPackets.clear();
            m_PassCtx.GBufferDrawPackets.reserve(batches.size());

            for (auto& kv : batches)
            {
                BatchInfo& bi = kv.second;
                bi.Packet.DrawAttribs.NumInstances = bi.Count;
                bi.Packet.DrawAttribs.FirstInstanceLocation = bi.FirstInstance;
                m_PassCtx.GBufferDrawPackets.push_back(bi.Packet);
            }

            // Sort packets for state change minimization (SB layout에는 영향 없음)
            std::sort(m_PassCtx.GBufferDrawPackets.begin(), m_PassCtx.GBufferDrawPackets.end(),
                [](const DrawPacket& a, const DrawPacket& b)
                {
                    if (a.PSO != b.PSO) return a.PSO < b.PSO;
                    if (a.SRB != b.SRB) return a.SRB < b.SRB;
                    if (a.VertexBuffer != b.VertexBuffer) return a.VertexBuffer < b.VertexBuffer;
                    if (a.IndexBuffer != b.IndexBuffer)  return a.IndexBuffer < b.IndexBuffer;

                    if (a.DrawAttribs.IndexType != b.DrawAttribs.IndexType) return a.DrawAttribs.IndexType < b.DrawAttribs.IndexType;
                    if (a.DrawAttribs.FirstIndexLocation != b.DrawAttribs.FirstIndexLocation) return a.DrawAttribs.FirstIndexLocation < b.DrawAttribs.FirstIndexLocation;
                    if (a.DrawAttribs.NumIndices != b.DrawAttribs.NumIndices) return a.DrawAttribs.NumIndices < b.DrawAttribs.NumIndices;
                    if (a.DrawAttribs.BaseVertex != b.DrawAttribs.BaseVertex) return a.DrawAttribs.BaseVertex < b.DrawAttribs.BaseVertex;

                    return a.DrawAttribs.FirstInstanceLocation < b.DrawAttribs.FirstInstanceLocation;
                });
        }

        // ============================================================
        //  Build Shadow packets + pack ObjectTableSBShadow (2-pass)
        // ============================================================
        {
            auto itPass = m_Passes.find("Shadow");
            ASSERT(itPass != m_Passes.end(), "Shadow pass not found.");

            auto* shadowPass = static_cast<ShadowRenderPass*>(itPass->second.get());
            ASSERT(shadowPass, "Shadow pass cast failed.");

            IPipelineState* shadowPSO = shadowPass->GetShadowPSO();
            IPipelineState* shadowMaskedPSO = shadowPass->GetShadowMaskedPSO();
            IShaderResourceBinding* shadowSRB = shadowPass->GetOpaqueShadowSRB();

            ASSERT(shadowPSO, "ShadowPSO is null.");
            ASSERT(shadowMaskedPSO, "ShadowMaskedPSO is null.");
            ASSERT(shadowSRB, "ShadowSRB is null.");

            std::unordered_map<DrawPacketKey, BatchInfo, DrawPacketKeyHasher> batches;
            batches.reserve(m_PassCtx.VisibleObjectIndexShadow.size() * 4);

            // Pass1 count
            uint32 totalInstances = 0;

            for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexShadow)
            {
                RenderScene::RenderObject& obj = objectsMutable[objectIndex];
                if (!obj.bCastShadow)
                    continue;

                const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
                ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

                IBuffer* vb = mesh->GetVertexBuffer();
                IBuffer* ib = mesh->GetIndexBuffer();
                ASSERT(vb && ib, "Mesh buffers are invalid.");

                const VALUE_TYPE indexType = mesh->GetIndexType();

                for (const auto& sec : mesh->GetSections())
                {
                    ASSERT(sec.IndexCount > 0, "Invalid section. IndexCount == 0.");

                    IPipelineState* pso = nullptr;
                    IShaderResourceBinding* srb = nullptr;

                    if (!obj.bAlphaMasked)
                    {
                        pso = shadowPSO;
                        srb = shadowSRB;
                    }
                    else
                    {
                        const uint32 slot = sec.MaterialSlot;
                        ASSERT(slot < static_cast<uint32>(obj.Materials.size()), "Material slot index out of bounds.");

                        Handle<MaterialRenderData> hRD = obj.Materials[slot];
                        ASSERT(hRD.IsValid(), "Material instance in object is null.");

                        MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(hRD);
                        ASSERT(rd, "Material render data is invalid.");

                        IShaderResourceBinding* maskedShadowSRB = rd->GetShadowSRB();
                        ASSERT(maskedShadowSRB, "Masked shadow SRB is null.");

                        pso = shadowMaskedPSO;
                        srb = maskedShadowSRB;
                    }

                    DrawPacketKey key = {};
                    key.PSO = pso;
                    key.SRB = srb;
                    key.VB = vb;
                    key.IB = ib;
                    key.IndexType = indexType;
                    key.NumIndices = sec.IndexCount;
                    key.FirstIndexLocation = sec.FirstIndex;
                    key.BaseVertex = static_cast<int32>(sec.BaseVertex);

                    auto it = batches.find(key);
                    if (it == batches.end())
                    {
                        BatchInfo bi = {};
                        bi.Packet = {};
                        bi.Packet.VertexBuffer = vb;
                        bi.Packet.IndexBuffer = ib;
                        bi.Packet.PSO = key.PSO;
                        bi.Packet.SRB = key.SRB;
                        bi.Packet.ObjectIndex = 0;

                        bi.Packet.DrawAttribs = {};
                        bi.Packet.DrawAttribs.NumIndices = key.NumIndices;
                        bi.Packet.DrawAttribs.FirstIndexLocation = key.FirstIndexLocation;
                        bi.Packet.DrawAttribs.BaseVertex = key.BaseVertex;
                        bi.Packet.DrawAttribs.IndexType = key.IndexType;
                        bi.Packet.DrawAttribs.NumInstances = 0;
                        bi.Packet.DrawAttribs.FirstInstanceLocation = 0;
                        bi.Packet.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

                        auto [newIt, ok] = batches.emplace(key, bi);
                        ASSERT(ok, "Failed to emplace Shadow batch.");
                        it = newIt;
                    }

                    it->second.Count += 1;
                    totalInstances += 1;
                }
            }

            ASSERT(totalInstances < DEFAULT_MAX_OBJECT_COUNT, "Increase object table capacity (Shadow).");

            // Prefix sum
            {
                uint32 cursor = 0;
                for (auto& kv : batches)
                {
                    BatchInfo& bi = kv.second;
                    bi.FirstInstance = cursor;
                    bi.Cursor = 0;
                    cursor += bi.Count;
                }
            }

            // Pass2 fill SBShadow
            {
                MapHelper<hlsl::ObjectConstants> map(ctx, m_pObjectTableSBShadow, MAP_WRITE, MAP_FLAG_DISCARD);
                hlsl::ObjectConstants* dst = map;

                for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexShadow)
                {
                    const RenderScene::RenderObject& obj = objectsMutable[objectIndex];
                    if (!obj.bCastShadow)
                        continue;

                    const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
                    ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

                    IBuffer* vb = mesh->GetVertexBuffer();
                    IBuffer* ib = mesh->GetIndexBuffer();
                    const VALUE_TYPE indexType = mesh->GetIndexType();

                    for (const auto& sec : mesh->GetSections())
                    {
                        IPipelineState* pso = nullptr;
                        IShaderResourceBinding* srb = nullptr;

                        if (!obj.bAlphaMasked)
                        {
                            pso = shadowPSO;
                            srb = shadowSRB;
                        }
                        else
                        {
                            const uint32 slot = sec.MaterialSlot;
                            Handle<MaterialRenderData> hRD = obj.Materials[slot];
                            MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(hRD);
                            IShaderResourceBinding* maskedShadowSRB = rd->GetShadowSRB();

                            pso = shadowMaskedPSO;
                            srb = maskedShadowSRB;
                        }

                        DrawPacketKey key = {};
                        key.PSO = pso;
                        key.SRB = srb;
                        key.VB = vb;
                        key.IB = ib;
                        key.IndexType = indexType;
                        key.NumIndices = sec.IndexCount;
                        key.FirstIndexLocation = sec.FirstIndex;
                        key.BaseVertex = static_cast<int32>(sec.BaseVertex);

                        auto it = batches.find(key);
                        ASSERT(it != batches.end(), "Batch not found (Shadow).");

                        BatchInfo& bi = it->second;

                        const uint32 writeIndex = bi.FirstInstance + bi.Cursor;
                        bi.Cursor += 1;

                        hlsl::ObjectConstants oc = {};
                        oc.World = obj.Transform;
                        oc.WorldInvTranspose = obj.Transform.Inversed().Transposed();

                        dst[writeIndex] = oc;
                    }
                }
            }

            // Finalize packets
            m_PassCtx.ShadowDrawPackets.clear();
            m_PassCtx.ShadowDrawPackets.reserve(batches.size());

            for (auto& kv : batches)
            {
                BatchInfo& bi = kv.second;
                bi.Packet.DrawAttribs.NumInstances = bi.Count;
                bi.Packet.DrawAttribs.FirstInstanceLocation = bi.FirstInstance;
                m_PassCtx.ShadowDrawPackets.push_back(bi.Packet);
            }

            std::sort(m_PassCtx.ShadowDrawPackets.begin(), m_PassCtx.ShadowDrawPackets.end(),
                [](const DrawPacket& a, const DrawPacket& b)
                {
                    if (a.PSO != b.PSO) return a.PSO < b.PSO;
                    if (a.SRB != b.SRB) return a.SRB < b.SRB;
                    if (a.VertexBuffer != b.VertexBuffer) return a.VertexBuffer < b.VertexBuffer;
                    if (a.IndexBuffer != b.IndexBuffer)  return a.IndexBuffer < b.IndexBuffer;

                    if (a.DrawAttribs.IndexType != b.DrawAttribs.IndexType) return a.DrawAttribs.IndexType < b.DrawAttribs.IndexType;
                    if (a.DrawAttribs.FirstIndexLocation != b.DrawAttribs.FirstIndexLocation) return a.DrawAttribs.FirstIndexLocation < b.DrawAttribs.FirstIndexLocation;
                    if (a.DrawAttribs.NumIndices != b.DrawAttribs.NumIndices) return a.DrawAttribs.NumIndices < b.DrawAttribs.NumIndices;
                    if (a.DrawAttribs.BaseVertex != b.DrawAttribs.BaseVertex) return a.DrawAttribs.BaseVertex < b.DrawAttribs.BaseVertex;

                    return a.DrawAttribs.FirstInstanceLocation < b.DrawAttribs.FirstInstanceLocation;
                });
        }

        const auto tEnd = Clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(tEnd - tBegin).count();

        std::cout << "[Renderer::Render] " << ms << " ms\n";

        // ------------------------------------------------------------
        // Execute passes
        // ------------------------------------------------------------
        for (const std::string& name : m_PassOrder)
        {
            RenderPassBase* pass = m_Passes[name].get();
            ASSERT(pass, "Pass is null.");
            pass->Execute(m_PassCtx);
        }

        wirePassOutputs();
    }

	void Renderer::EndFrame()
	{
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->EndFrame(m_PassCtx);
		}
	}

	void Renderer::ReleaseSwapChainBuffers()
	{
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->ReleaseSwapChainBuffers(m_PassCtx);
		}
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		ASSERT(width != 0 && height != 0, "Invalid size.");

		m_Width = width;
		m_Height = height;

		m_PassCtx.BackBufferWidth = width;
		m_PassCtx.BackBufferHeight = height;

		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->OnResize(m_PassCtx, width, height);
		}

		wirePassOutputs();
	}

	Handle<TextureRenderData> Renderer::CreateTexture(const TextureAsset& asset)
	{
		ASSERT(m_pCache, "Cache is null.");
		return m_pCache->GetOrCreateTextureRenderData(asset);
	}

	bool Renderer::DestroyTexture(Handle<TextureRenderData> hTex)
	{
		ASSERT(m_pCache, "Cache is null.");
		return m_pCache->DestroyTextureRenderData(hTex);
	}

	Handle<MaterialRenderData> Renderer::CreateMaterial(MaterialInstance& inst, bool bCastShadow, bool bAlphaMasked)
	{
		ASSERT(m_pCache, "Cache is null.");
		IPipelineState* shadowMaskedPSO = nullptr;
		{
			auto it = m_Passes.find("Shadow");
			if (it != m_Passes.end())
			{
				if (auto* shadowPass = static_cast<ShadowRenderPass*>(it->second.get()))
				{
					shadowMaskedPSO = shadowPass->GetShadowMaskedPSO();
				}
			}
		}
		IPipelineState* pShadowPsoForMat = (bCastShadow && bAlphaMasked) ? shadowMaskedPSO : nullptr;

		const std::string& passName = inst.GetRenderPass();
		auto passIter = m_Passes.find(passName);
		ASSERT(passIter != m_Passes.end(), "Render pass (%s) is not set in renderer.", passName);
		ASSERT(passIter->second, "Render pass (%s) is null", passName);

		inst.GetGraphicsPipelineDesc().pRenderPass = passIter->second.get()->GetRHIRenderPass();

		return m_pCache->GetOrCreateMaterialRenderData(
			inst,
			bCastShadow,
			bAlphaMasked,
			m_CreateInfo.pImmediateContext.RawPtr(),
			m_pPipelineStateManager.get(),
			m_pMaterialStaticBinder.get(),
			pShadowPsoForMat);
	}

	bool Renderer::DestroyMaterial(Handle<MaterialRenderData> hMesh)
	{
		ASSERT(m_pCache, "Cache is null.");
		return m_pCache->DestroyMaterialRenderData(hMesh);
	}

	Handle<StaticMeshRenderData> Renderer::CreateStaticMesh(const StaticMeshAsset& asset)
	{
		ASSERT(m_pCache, "Cache is null.");
		return m_pCache->GetOrCreateStaticMeshRenderData(asset, m_CreateInfo.pImmediateContext.RawPtr());
	}

	bool Renderer::DestroyStaticMesh(Handle<StaticMeshRenderData> hMesh)
	{
		ASSERT(m_pCache, "Cache is null.");
		return m_pCache->DestroyStaticMeshRenderData(hMesh);
	}

	const std::unordered_map<std::string, uint64> Renderer::GetPassDrawCallCountTable() const
	{
		std::unordered_map<std::string, uint64> drawCallTable;
		for (auto& passPair : m_Passes)
		{
			const std::string& name = passPair.first;
			uint64 drawCallCount = passPair.second->GetDrawCallCount();

			drawCallTable[name] = drawCallCount;
		}
		return drawCallTable;
	}

	void Renderer::uploadObjectIndexInstance(IDeviceContext* pCtx, uint32 objectIndex)
	{
		ASSERT(pCtx, "Context is null.");
		ASSERT(m_pObjectIndexVB, "Object index VB is null.");

		MapHelper<uint32> map(pCtx, m_pObjectIndexVB, MAP_WRITE, MAP_FLAG_DISCARD);
		*map = objectIndex;
	}

	void Renderer::wirePassOutputs()
	{
		if (auto* shadow = static_cast<ShadowRenderPass*>(m_Passes["Shadow"].get()))
		{
			m_PassCtx.pShadowMapSrv = shadow->GetShadowMapSRV();
		}

		if (auto* gb = static_cast<GBufferRenderPass*>(m_Passes["GBuffer"].get()))
		{
			for (uint32 i = 0; i < RenderPassContext::NUM_GBUFFERS; ++i)
				m_PassCtx.pGBufferSrv[i] = gb->GetGBufferSRV(i);

			m_PassCtx.pDepthSrv = gb->GetDepthSRV();
		}

		if (auto* light = static_cast<LightingRenderPass*>(m_Passes["Lighting"].get()))
		{
			m_PassCtx.pLightingSrv = light->GetLightingSRV();
		}
	}

	void Renderer::addPass(std::unique_ptr<RenderPassBase> pass)
	{
		ASSERT(pass, "Pass is null.");

		const char* name = pass->GetName();
		ASSERT(name && name[0] != '\0', "Pass name is empty.");

		auto it = m_Passes.find(name);
		ASSERT(it == m_Passes.end(), "Duplicate pass name.");

		m_PassOrder.push_back(name);
		m_Passes.emplace(name, std::move(pass));
	}
} // namespace shz
