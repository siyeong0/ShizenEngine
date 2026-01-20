/*
 *  Copyright 2019-2025 Diligent Graphics LLC
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
#include "ThirdParty/imgui/imgui.h"
#include <cstddef>
#include "ThirdParty/imgui/imgui.h"
#include "ImGuiImplShizen.hpp"
#include "ImGuiShizenRenderer.hpp"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Math/Math.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{

	ImGuiShizenCreateInfo::ImGuiShizenCreateInfo(IRenderDevice* _pDevice, TEXTURE_FORMAT _BackBufferFmt, TEXTURE_FORMAT _DepthBufferFmt) noexcept
		: pDevice{ _pDevice }
		, BackBufferFmt{ _BackBufferFmt }
		, DepthBufferFmt{ _DepthBufferFmt }
	{
	}

	ImGuiShizenCreateInfo::ImGuiShizenCreateInfo(IRenderDevice* _pDevice, const SwapChainDesc& _SCDesc) noexcept
		: ImGuiShizenCreateInfo{ _pDevice, _SCDesc.ColorBufferFormat, _SCDesc.DepthBufferFormat }
	{
	}


	ImGuiImplShizen::ImGuiImplShizen(const ImGuiShizenCreateInfo& CI)
	{
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;

		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		m_pRenderer = std::make_unique<ImGuiShizenRenderer>(CI);
	}

	ImGuiImplShizen::~ImGuiImplShizen()
	{
		m_pRenderer->InvalidateDeviceObjects();
		ImGui::DestroyContext();
	}

	void ImGuiImplShizen::NewFrame(uint32 RenderSurfaceWidth, uint32 RenderSurfaceHeight, SURFACE_TRANSFORM SurfacePreTransform)
	{
		m_pRenderer->NewFrame(RenderSurfaceWidth, RenderSurfaceHeight, SurfacePreTransform);
		ImGui::NewFrame();
	}

	void ImGuiImplShizen::EndFrame()
	{
		ImGui::EndFrame();
	}

	void ImGuiImplShizen::Render(IDeviceContext* pCtx)
	{
		// No need to call ImGui::EndFrame as ImGui::Render calls it automatically
		ImGui::Render();
		m_pRenderer->RenderDrawData(pCtx, ImGui::GetDrawData());
	}

	// Use if you want to reset your rendering device without losing ImGui state.
	void ImGuiImplShizen::InvalidateDeviceObjects()
	{
		m_pRenderer->InvalidateDeviceObjects();
	}

	void ImGuiImplShizen::CreateDeviceObjects()
	{
		m_pRenderer->CreateDeviceObjects();
	}

} // namespace shz
