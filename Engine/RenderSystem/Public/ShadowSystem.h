#pragma once
#include <array>
#include <vector>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Math/Math.h"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IBuffer.h"

namespace shz
{
    namespace hlsl
    {
#include "Shaders/HLSL_Structures.hlsli"
    }

    class Renderer;
    struct View;

    class ShadowSystem final
    {
    public:
        struct CreateInfo final
        {
            uint32 ShadowMapResolution = 4096;
            uint32 NumCascades = 4;

            // Frustum partition
            float PartitioningFactor = 0.95f;  // lambda (0=linear,1=log)
            float CascadeTransitionRegion = 0.10f;

            // Stabilization
            bool  SnapCascades = true;         // texel snapping in light-space
            bool  QuantizeExtents = true;      // extent -> texel multiple
            bool  EqualizeExtents = true;      // force square extents for XY

            float ZPadding = 20.0f;            // push zn/zf by padding (light-space)

            // Bias / PCF
            float ReceiverPlaneDepthBiasClamp = 0.02f;
            float FixedDepthBias = 0.0005f;
            int   FixedFilterSize = 5;
        };

        ShadowSystem() = default;
        ShadowSystem(const ShadowSystem&) = delete;
        ShadowSystem& operator=(const ShadowSystem&) = delete;
        ~ShadowSystem() = default;

        void Initialize(Renderer& renderer, const CreateInfo& ci);
        void Shutdown();

        void UpdateShadowMatrices(Renderer& renderer, const View& mainView, const float3& lightDirWs);
        void InstallPasses(Renderer& renderer);

        uint32 GetNumCascades() const { return m_CI.NumCascades; }
        uint32 GetResolution()  const { return m_CI.ShadowMapResolution; }

    private:
        static constexpr uint32 kMaxCascades = 8;

        static void BuildLightViewBasis(const float3& lightDirWs, float3& X, float3& Y, float3& Z);

        static hlsl::CascadeAttribs& GetCascadeRef(hlsl::ShadowMapAttribs& A, uint32 idx);
        static void SetCascadeCamSpaceZEnd(hlsl::ShadowMapAttribs& A, uint32 idx, float z);

        static void ComputeCascadeSplits(
            float camNear,
            float camFar,
            uint32 numCascades,
            float lambda,
            float* outSplitZ); // length=numCascades (endZ each)

        static void BuildFrustumCornersWS_ForZRange(
            const Matrix4x4& cameraWorld,
            float tanHalfFovY,
            float aspect,
            float zNear,
            float zFar,
            float3 outCornersWS[8]);

        void BuildCascade_FrustumFitStabilized(
            uint32 cascadeIdx,
            const float3 frustumCornersWS[8],
            const float3& lightDirWs,
            float shadowMapRes);

    private:
        CreateInfo m_CI = {};

        uint64 m_ShadowMapTexId = 0;
        uint64 m_ShadowMapSRVId = 0;
        std::array<uint64, kMaxCascades> m_ShadowMapCascadeDSVIds = {};

        uint64 m_ShadowAttribsCBId = 0;

        hlsl::ShadowMapAttribs m_ShadowAttribs = {};
    };
} // namespace shz
