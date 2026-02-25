#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
    class Renderer;

    class AmbientOcclusionSystem final
    {
    public:
        AmbientOcclusionSystem() = default;
        AmbientOcclusionSystem(const AmbientOcclusionSystem&) = delete;
        AmbientOcclusionSystem& operator=(const AmbientOcclusionSystem&) = delete;
        ~AmbientOcclusionSystem() = default;

        void Initialize(Renderer& renderer);
        void InstallPasses(Renderer& renderer);

    private:
        // Pass0: Half-res GTAO (Compute) -> AmbientOcclusionMapHalfRaw
        RefCntAutoPtr<IPipelineState>         m_pGTAOCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pGTAOSRB;

        // Pass1/2: Half-res Bilateral Blur (Compute, separable)
        RefCntAutoPtr<IPipelineState>         m_pBlurXCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pBlurXSRB;

        RefCntAutoPtr<IPipelineState>         m_pBlurYCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pBlurYSRB;

        // Pass3: Full-res Bilateral Upsample (Compute) -> AmbientOcclusionMap
        RefCntAutoPtr<IPipelineState>         m_pUpsampleCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pUpsampleSRB;

        // Shaders
        std::string m_GTAOCS = "GTAO.hlsl";
        std::string m_BilateralBlurCS = "AOBilateralBlur.hlsl";
        std::string m_UpsampleCS = "AOUpsample.hlsl";

    private:
        static constexpr uint32 AO_GROUP_SIZE_X = 8;
        static constexpr uint32 AO_GROUP_SIZE_Y = 8;

        static constexpr uint32 BLUR_GROUP_SIZE_X = 8;
        static constexpr uint32 BLUR_GROUP_SIZE_Y = 8;

        static constexpr uint32 UPSAMPLE_GROUP_SIZE_X = 8;
        static constexpr uint32 UPSAMPLE_GROUP_SIZE_Y = 8;
    };
} // namespace shz