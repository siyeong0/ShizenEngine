#pragma once

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "FirstPersonCamera.h"

namespace shz
{
    class ShizenEngine final : public SampleBase
    {
    public:
        virtual void Initialize(const SampleInitInfo& InitInfo) override final;

        virtual void Render() override final;
        virtual void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

        virtual const Char* GetSampleName() const override final { return "Shizen Engine"; }

    private:
        std::unique_ptr<Renderer>    m_pRenderer = nullptr;
        std::unique_ptr<RenderScene> m_pRenderScene = nullptr;

        std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

        ViewFamily m_ViewFamily = {};
        FirstPersonCamera m_Camera;

        RenderObjectId m_HelmetId = {};
    };
} // namespace shz
