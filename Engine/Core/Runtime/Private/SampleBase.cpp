/*
 *  Copyright 2019-2024 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "pch.h"
#include "Platforms/Common/PlatformDefinitions.h"
#include "Engine/Core/Common/Public/Errors.hpp"
#include "Engine/Core/Runtime/Public/SampleBase.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "ThirdParty/imgui/imgui.h"

namespace shz
{

    void SampleBase::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
    {
        Attribs.EngineCI.Features = DeviceFeatures{ DEVICE_FEATURE_STATE_OPTIONAL };

        Attribs.EngineCI.Features.TransferQueueTimestampQueries = DEVICE_FEATURE_STATE_DISABLED;

        switch (Attribs.DeviceType)
        {

#if D3D12_SUPPORTED
        case RENDER_DEVICE_TYPE_D3D12:
        {
            EngineD3D12CreateInfo& EngineD3D12CI = static_cast<EngineD3D12CreateInfo&>(Attribs.EngineCI);
            //EngineD3D12CI.GPUDescriptorHeapDynamicSize[0] = 32768;
            //EngineD3D12CI.GPUDescriptorHeapSize[1] = 1024;
            //EngineD3D12CI.GPUDescriptorHeapDynamicSize[1] = 2048 - 128;
            //EngineD3D12CI.DynamicDescriptorAllocationChunkSize[0] = 32;
            //EngineD3D12CI.DynamicDescriptorAllocationChunkSize[1] = 8; // D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        }
        break;
#endif
        default:
            LOG_ERROR_AND_THROW("Unknown device type");
            break;
        }
    }

    float4x4 SampleBase::GetAdjustedProjectionMatrix(float FOV, float NearPlane, float FarPlane) const
    {
        const auto& SCDesc = m_pSwapChain->GetDesc();

        float AspectRatio = static_cast<float>(SCDesc.Width) / static_cast<float>(SCDesc.Height);
        float XScale, YScale;
        if (SCDesc.PreTransform == SURFACE_TRANSFORM_ROTATE_90 ||
            SCDesc.PreTransform == SURFACE_TRANSFORM_ROTATE_270 ||
            SCDesc.PreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90 ||
            SCDesc.PreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270)
        {
            // When the screen is rotated, vertical FOV becomes horizontal FOV
            XScale = 1.f / std::tan(FOV / 2.f);
            // Aspect ratio is inversed
            YScale = XScale * AspectRatio;
        }
        else
        {
            YScale = 1.f / std::tan(FOV / 2.f);
            XScale = YScale / AspectRatio;
        }

        float4x4 Proj = float4x4::Zero();
        Proj._m00 = XScale;
        Proj._m11 = YScale;
        Proj.SetNearFarClipPlanes(NearPlane, FarPlane, m_pDevice->GetDeviceInfo().NDC.MinZ == -1);
        return Proj;
    }

    float4x4 SampleBase::GetSurfacePretransformMatrix(const float3& f3CameraViewAxis) const
    {
        const auto& SCDesc = m_pSwapChain->GetDesc();
        switch (SCDesc.PreTransform)
        {
        case SURFACE_TRANSFORM_ROTATE_90:
            // The image content is rotated 90 degrees clockwise.
            return float4x4::RotationAxis(f3CameraViewAxis, -PI / 2.f);

        case SURFACE_TRANSFORM_ROTATE_180:
            // The image content is rotated 180 degrees clockwise.
            return float4x4::RotationAxis(f3CameraViewAxis, -PI);

        case SURFACE_TRANSFORM_ROTATE_270:
            // The image content is rotated 270 degrees clockwise.
            return float4x4::RotationAxis(f3CameraViewAxis, -PI * 3.f / 2.f);

        case SURFACE_TRANSFORM_OPTIMAL:
            ASSERT(false, "SURFACE_TRANSFORM_OPTIMAL is only valid as parameter during swap chain initialization.");
            return float4x4::Identity();

        case SURFACE_TRANSFORM_HORIZONTAL_MIRROR:
        case SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90:
        case SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180:
        case SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270:
            ASSERT(false, "Mirror transforms are not supported");
            return float4x4::Identity();

        default:
            return float4x4::Identity();
        }
    }

    void SampleBase::Initialize(const SampleInitInfo& InitInfo)
    {
        m_pEngineFactory = InitInfo.pEngineFactory;
        m_pDevice = InitInfo.pDevice;
        m_pSwapChain = InitInfo.pSwapChain;
        m_pImmediateContext = InitInfo.ppContexts[0];
        m_pDeferredContexts.resize(InitInfo.NumDeferredCtx);
        for (uint32 ctx = 0; ctx < InitInfo.NumDeferredCtx; ++ctx)
            m_pDeferredContexts[ctx] = InitInfo.ppContexts[InitInfo.NumImmediateCtx + ctx];
        m_pImGui = InitInfo.pImGui;
        ImGui::StyleColorsShizen();

        const auto& SCDesc = m_pSwapChain->GetDesc();
        // If the swap chain color buffer format is a non-sRGB UNORM format,
        // we need to manually convert pixel shader output to gamma space.
        m_ConvertPSOutputToGamma = (SCDesc.ColorBufferFormat == TEX_FORMAT_RGBA8_UNORM ||
            SCDesc.ColorBufferFormat == TEX_FORMAT_BGRA8_UNORM);
    }

} // namespace shz
