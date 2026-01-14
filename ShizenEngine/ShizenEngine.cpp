#include "ShizenEngine.h"
#include "Engine/AssetRuntime/Public/AssimpImporter.h"

namespace shz
{
    SampleBase* CreateSample()
    {
        return new ShizenEngine();
    }

    void ShizenEngine::Initialize(const SampleInitInfo& InitInfo)
    {
        SampleBase::Initialize(InitInfo);

        // 1) AssetManager (CPU assets owner)
        m_pAssetManager = std::make_unique<AssetManager>();

        // 2) Renderer
        m_pRenderer = std::make_unique<Renderer>();

        RendererCreateInfo rendererCreateInfo = {};
        rendererCreateInfo.pEngineFactory = m_pEngineFactory;
        rendererCreateInfo.pDevice = m_pDevice;
        rendererCreateInfo.pImmediateContext = m_pImmediateContext;
        rendererCreateInfo.pDeferredContexts = m_pDeferredContexts;
        rendererCreateInfo.pSwapChain = m_pSwapChain;
        rendererCreateInfo.pImGui = m_pImGui;
        rendererCreateInfo.BackBufferWidth = m_pSwapChain->GetDesc().Width;
        rendererCreateInfo.BackBufferHeight = m_pSwapChain->GetDesc().Height;
        rendererCreateInfo.pAssetManager = m_pAssetManager.get(); // NEW

        m_pRenderer->Initialize(rendererCreateInfo);

        m_pRenderScene = std::make_unique<RenderScene>();

        m_Camera.SetProjAttribs(
            0.1f,
            100.0f,
            static_cast<float>(rendererCreateInfo.BackBufferWidth) / rendererCreateInfo.BackBufferHeight,
            PI / 4.0f,
            SURFACE_TRANSFORM_IDENTITY);

        m_ViewFamily.Views.push_back({});
        m_ViewFamily.Views[0].Viewport = {};

        // 기존 테스트 큐브는 그대로 renderer-owned (asset 아님)
        MeshHandle cubeHandle = m_pRenderer->CreateCubeMesh();
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ -2, -2, -2 }, { PI / 4,0,0 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ -2, -2,  0 }, { -PI / 4,0,0 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ -2, -2,  2 }, { 0,PI / 4,0 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ 0, -2, -2 }, { 0,-PI / 4,0 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ 0, -2,  0 }, { 0,0,0 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ 0, -2,  2 }, { 0,0,PI / 4 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ 2, -2, -2 }, { 0,0,-PI / 4 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ 2, -2,  0 }, { PI / 4,-PI / 4,0 }, { 1,1,1 }));
        m_pRenderScene->AddObject(cubeHandle, Matrix4x4::TRS({ 2, -2,  2 }, { 0,-PI / 4,PI / 4 }, { 1,1,1 }));

        // ------------------------------------------------------------
        // Assets: FlightHelmet (CPU load -> AssetManager register -> Renderer create GPU mesh)
        // ------------------------------------------------------------
        StaticMeshAsset flightHelmetMeshAsset;
        if (!AssimpImporter::LoadStaticMeshAsset(
            "C:/Dev/ShizenEngine/ShizenEngine/Assets/FlightHelmet/glTF/FlightHelmet.gltf",
            &flightHelmetMeshAsset))
        {
            std::cout << "Load Failed" << std::endl;
        }

        // NEW: register CPU asset and get handle
        StaticMeshAssetHandle helmetAssetHandle = m_pAssetManager->RegisterStaticMesh(flightHelmetMeshAsset);

        // Renderer now takes asset handle
        MeshHandle flightHelmetMeshHandle = m_pRenderer->CreateStaticMesh(helmetAssetHandle);

        m_HelmetId = m_pRenderScene->AddObject(flightHelmetMeshHandle, Matrix4x4::TRS({ 0, 0, 8 }, { 0,0,0 }, { 5,5,5 }));
        auto dummy = m_pRenderScene->AddObject(flightHelmetMeshHandle, Matrix4x4::TRS({ 5, 3, 8 }, { 0,0,0 }, { 5,5,5 }));
        (void)dummy;
    }

    void ShizenEngine::Render()
    {
        m_ViewFamily.FrameIndex++;

        m_pRenderer->BeginFrame();
        m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
        m_pRenderer->EndFrame();
    }

    void ShizenEngine::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
    {
        SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

        float dt = static_cast<float>(ElapsedTime);

        m_Camera.Update(m_InputController, dt);

        m_ViewFamily.DeltaTime = dt;
        m_ViewFamily.Views[0].ViewMatrix = m_Camera.GetViewMatrix();
        m_ViewFamily.Views[0].ProjMatrix = m_Camera.GetProjMatrix();

        m_pRenderScene->SetObjectTransform(
            m_HelmetId,
            Matrix4x4::TRS({ 0, 0, 8 }, { 0,static_cast<float>(CurrTime),0 }, { 5,5,5 }));
    }
} // namespace shz
