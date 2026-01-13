#include <memory>
#include <iomanip>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdio>

#include <Windows.h>
#include <crtdbg.h>

#include "Engine/Core/Runtime/Public/NativeAppBase.hpp"
#include "Engine/Core/Common/Public/StringTools.hpp"
#include "Engine/Core/Common/Public/Timer.hpp"

#include <windows.h>
#include <cstdio>

static void OpenConsole()
{
	if (GetConsoleWindow() != nullptr)
		return;

	AllocConsole();

	FILE* fp = nullptr;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	freopen_s(&fp, "CONIN$", "r", stdin);

	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	SetConsoleTitleW(L"ShizenEngine Console");
}

using namespace shz;

std::unique_ptr<NativeAppBase> g_pTheApp;

LRESULT CALLBACK MessageProc(HWND, UINT, WPARAM, LPARAM);
// Main
int WINAPI WinMain(
	_In_ HINSTANCE     hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR         lpCmdLine,
	_In_ int           nShowCmd)
{
#if defined(_DEBUG) || defined(DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	OpenConsole();

	g_pTheApp.reset(CreateApplication());

	LPCSTR CmdLine = GetCommandLineA();

	const std::vector<std::string> Args = SplitString(CmdLine, CmdLine + strlen(CmdLine));

	std::vector<const char*> ArgsV(Args.size());
	for (size_t i = 0; i < Args.size(); ++i)
		ArgsV[i] = Args[i].c_str();

	AppBase::CommandLineStatus CmdLineStatus = g_pTheApp->ProcessCommandLine(static_cast<int>(ArgsV.size()), ArgsV.data());
	if (CmdLineStatus == AppBase::CommandLineStatus::Help)
		return 0;
	else if (CmdLineStatus == AppBase::CommandLineStatus::Error)
		return -1;

	const char* AppTitle = g_pTheApp->GetAppTitle();

#ifdef UNICODE
	LPCWSTR WindowClassName = L"SampleApp";
#else
	LPCSTR WindowClassName = "SampleApp";
#endif

	// Register our window class
	WNDCLASSEX wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, MessageProc,
					   0L, 0L, hInstance, NULL, NULL, NULL, NULL, WindowClassName, NULL };
	RegisterClassEx(&wcex);

	int DesiredWidth = 0;
	int DesiredHeight = 0;
	g_pTheApp->GetDesiredInitialWindowSize(DesiredWidth, DesiredHeight);
	// Create a window
	LONG WindowWidth = DesiredWidth > 0 ? DesiredWidth : 1280;
	LONG WindowHeight = DesiredHeight > 0 ? DesiredHeight : 1024;
	RECT rc = { 0, 0, WindowWidth, WindowHeight };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND wnd = CreateWindowA("SampleApp", AppTitle,
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
	if (!wnd)
	{
		std::cerr << "Failed to create a window";
		return -1;
	}

	if (!g_pTheApp->OnWindowCreated(wnd, WindowWidth, WindowHeight))
	{
		std::cerr << "Failed to initialize application " << AppTitle;
		return -1;
	}

	NativeAppBase::GoldenImageMode GoldenImgMode = g_pTheApp->GetGoldenImageMode();
	if (GoldenImgMode != NativeAppBase::GoldenImageMode::None)
	{
		g_pTheApp->Update(0, 0);
		g_pTheApp->Render();
		// Dear imgui windows that don't have initial size are not rendered in the first frame,
		// see https://github.com/ocornut/imgui/issues/2949
		g_pTheApp->Update(0, 0);
		g_pTheApp->Render();
		g_pTheApp->Present();
		int ExitCode = g_pTheApp->GetExitCode();
		g_pTheApp.reset();
		return ExitCode;
	}

	ShowWindow(wnd, nShowCmd);
	UpdateWindow(wnd);

	AppTitle = g_pTheApp->GetAppTitle();

	Timer Timer;

	double PrevTime = Timer.GetElapsedTime();
	double filteredFrameTime = 0.0;

	// Main message loop
	MSG msg = { 0 };
	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			double CurrTime = Timer.GetElapsedTime();
			double ElapsedTime = CurrTime - PrevTime;
			PrevTime = CurrTime;

			if (g_pTheApp->IsReady())
			{
				g_pTheApp->Update(CurrTime, ElapsedTime);

				g_pTheApp->Render();

				g_pTheApp->Present();

				double filterScale = 0.2;
				filteredFrameTime = filteredFrameTime * (1.0 - filterScale) + filterScale * ElapsedTime;
				std::stringstream fpsCounterSS;
				fpsCounterSS << AppTitle << " - " << std::fixed << std::setprecision(1) << filteredFrameTime * 1000;
				fpsCounterSS << " ms (" << 1.0 / filteredFrameTime << " fps)";
				SetWindowTextA(wnd, fpsCounterSS.str().c_str());
			}
		}
	}

	g_pTheApp.reset();

	return (int)msg.wParam;
}

// Called every time the NativeNativeAppBase receives a message
LRESULT CALLBACK MessageProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (g_pTheApp)
	{
		LRESULT res = g_pTheApp->HandleWin32Message(wnd, message, wParam, lParam);
		if (res != 0)
			return res;
	}

	switch (message)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(wnd, &ps);
		EndPaint(wnd, &ps);
		return 0;
	}
	case WM_SIZE: // Window size has been changed
		if (g_pTheApp)
		{
			g_pTheApp->WindowResize(LOWORD(lParam), HIWORD(lParam));
		}
		return 0;

	case WM_CHAR:
		if (wParam == VK_ESCAPE && (g_pTheApp->GetHotKeyFlags() & HOT_KEY_FLAG_ALLOW_EXIT_ON_ESC))
			PostQuitMessage(0);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_GETMINMAXINFO:
	{
		LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
		lpMMI->ptMinTrackSize.x = 320;
		lpMMI->ptMinTrackSize.y = 240;
		return 0;
	}

	default:
		return DefWindowProc(wnd, message, wParam, lParam);
	}
}
