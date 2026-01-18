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
#include "Platforms/Win64/Public/WinHPreface.h"
#include <Windows.h>
#include "Platforms/Win64/Public/WinHPostface.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiImplWin64.hpp"
#include "ThirdParty/imgui/backends/imgui_impl_win32.h"
#include "Primitives/DebugUtilities.hpp"

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace shz
{

	std::unique_ptr<ImGuiImplWin64> ImGuiImplWin64::Create(const ImGuiShizenCreateInfo& CI, HWND hWnd)
	{
		return std::make_unique<ImGuiImplWin64>(CI, hWnd);
	}

	ImGuiImplWin64::ImGuiImplWin64(const ImGuiShizenCreateInfo& CI, HWND hWnd) :
		ImGuiImplShizen{ CI }
	{
		ImGui_ImplWin32_Init(hWnd);
	}

	ImGuiImplWin64::~ImGuiImplWin64()
	{
		ImGui_ImplWin32_Shutdown();
	}

	void ImGuiImplWin64::NewFrame(uint32 RenderSurfaceWidth, uint32 RenderSurfaceHeight, SURFACE_TRANSFORM SurfacePreTransform)
	{
		ASSERT(SurfacePreTransform == SURFACE_TRANSFORM_IDENTITY, "Unexpected surface pre-transform");

		ImGui_ImplWin32_NewFrame();
		ImGuiImplShizen::NewFrame(RenderSurfaceWidth, RenderSurfaceHeight, SurfacePreTransform);

#ifdef SHZ_DEBUG
		{
			ImGuiIO& io = ImGui::GetIO();
			ASSERT(io.DisplaySize.x == 0 || io.DisplaySize.x == static_cast<float>(RenderSurfaceWidth),
				"Render surface width (", RenderSurfaceWidth, ") does not match io.DisplaySize.x (", io.DisplaySize.x, ")");
			ASSERT(io.DisplaySize.y == 0 || io.DisplaySize.y == static_cast<float>(RenderSurfaceHeight),
				"Render surface height (", RenderSurfaceHeight, ") does not match io.DisplaySize.y (", io.DisplaySize.y, ")");
		}
#endif
	}

	// Allow compilation with old Windows SDK. MinGW doesn't have default _WIN32_WINNT/WINVER versions.
#ifndef WM_MOUSEWHEEL
#    define WM_MOUSEWHEEL 0x020A
#endif
#ifndef WM_MOUSEHWHEEL
#    define WM_MOUSEHWHEEL 0x020E
#endif

	LRESULT ImGuiImplWin64::Win32_ProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui::GetCurrentContext() == NULL)
			return 0;

		LRESULT res = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

		ImGuiIO& io = ImGui::GetIO();
		switch (msg)
		{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
			return io.WantCaptureMouse ? 1 : 0;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
		case WM_CHAR:
			return io.WantCaptureKeyboard ? 1 : 0;
		case WM_SETCURSOR:
			return res;
		}

		return res;
	}

} // namespace shz
