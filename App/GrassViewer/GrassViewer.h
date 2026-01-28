#pragma once

#include <string>
#include <vector>
#include <memory>

#include <flecs.h>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/AssetManager/Public/AssetTypeTraits.h"

#include "Engine/Framework/Public/FirstPersonCamera.h"

#include "Engine/Physics/Public/Physics.h"

namespace shz
{
    class GrassViewer final : public SampleBase
    {
    public:
        void Initialize(const SampleInitInfo& InitInfo) override final;

        void Render() override final;
        void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

        void ReleaseSwapChainBuffers() override final;
        void WindowResize(uint32 Width, uint32 Height) override final;

        const Char* GetSampleName() const override final { return "GrassViewer"; }

    protected:
        void UpdateUI() override final;

    public:
        struct ViewportState final
        {
            uint32 Width = 1;
            uint32 Height = 1;
        };

    private:
        // ECS components (minimal; 버전 차이 없는 단순 POD)
        struct CName final { std::string Value = {}; };

        struct CTransform final
        {
            float3 Position = { 0, 0, 0 };
            float3 Rotation = { 0, 0, 0 };
            float3 Scale = { 1, 1, 1 };
        };

        struct CRenderMesh final
        {
            std::string Path = {};
            AssetRef<StaticMesh> MeshRef = {};
            StaticMeshRenderData Mesh = {};
            bool bCastShadow = true;
            bool bAlphaMasked = false;
        };

        struct CRenderObjectHandle final
        {
            Handle<RenderScene::RenderObject> ObjectId = {};
        };

        struct CPhysicsBody final
        {
            // BodyID는 Jolt 타입이라 include 상황에 따라 불편하면 uint64로 바꿔도 됨
            // 지금은 "붙일 준비"만.
            bool bValid = false;
        };

    private:
        void RegisterEcsComponents();
        void BuildSceneOnce(); // 기존 로딩 로직 그대로

    private:
        std::unique_ptr<Renderer>     m_pRenderer = nullptr;
        std::unique_ptr<RenderScene>  m_pRenderScene = nullptr;
        std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

        RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

        // ECS (world 기본 생성자 없어서 포인터로)
        std::unique_ptr<flecs::world> m_pEcs = nullptr;

        // Physics
        std::unique_ptr<Physics> m_pPhysics = nullptr;

        ViewportState     m_Viewport = {};
        ViewFamily        m_ViewFamily = {};
        FirstPersonCamera m_Camera = {};

        RenderScene::LightObject         m_GlobalLight = {};
        Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

        float m_Speed = 3.0f;
    };
} // namespace shz
