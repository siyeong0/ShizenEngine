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

static void OpenConsole()
{
	if (GetConsoleWindow() != nullptr)
	{
		return;
	}

	if (!AttachConsole(ATTACH_PARENT_PROCESS))
	{
		AllocConsole();
	}

	FILE* fp = nullptr;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	freopen_s(&fp, "CONIN$", "r", stdin);

	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	SetConsoleTitleW(L"GrassViewer Console");
}

static void WaitConsoleOnExit()
{
	system("pause");
}

using namespace shz;

std::unique_ptr<NativeAppBase> g_pEngine;

LRESULT CALLBACK MessageProc(HWND, UINT, WPARAM, LPARAM);

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
	atexit(WaitConsoleOnExit);

	g_pEngine.reset(CreateApplication());

	LPCSTR CmdLine = GetCommandLineA();
	const std::vector<std::string> Args = SplitString(CmdLine, CmdLine + strlen(CmdLine));
	std::vector<const char*> ArgsV(Args.size());
	for (size_t i = 0; i < Args.size(); ++i)
	{
		ArgsV[i] = Args[i].c_str();
	}

	AppBase::CommandLineStatus CmdLineStatus = g_pEngine->ProcessCommandLine(static_cast<int>(ArgsV.size()), ArgsV.data());
	if (CmdLineStatus == AppBase::CommandLineStatus::Error) return -1;

	const char* AppTitle = g_pEngine->GetAppTitle();
	LPCWSTR WindowClassName = L"GrassViewer";

	WNDCLASSEX wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, MessageProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, WindowClassName, NULL };
	RegisterClassEx(&wcex);

	int desiredWidth = 0;
	int desiredHeight = 0;
	g_pEngine->GetDesiredInitialWindowSize(desiredWidth, desiredHeight);

	LONG windowWidth = desiredWidth > 0 ? desiredWidth : 1920;
	LONG windowHeight = desiredHeight > 0 ? desiredHeight : 1080;
	RECT rc = { 0, 0, windowWidth, windowHeight };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);
	const int winW = rc.right - rc.left;
	const int winH = rc.bottom - rc.top;
	int x = (screenW - winW) / 2 - 80;
	int y = (screenH - winH) / 2 - 80;
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	HWND wnd = CreateWindowA(
		"GrassViewer", AppTitle, WS_OVERLAPPEDWINDOW,
		x, y, winW, winH,
		NULL, NULL, hInstance, NULL);

	if (!wnd)
	{
		std::cerr << "Failed to create a window";
		return -1;
	}

	if (!g_pEngine->OnWindowCreated(wnd, windowWidth, windowHeight))
	{
		std::cerr << "Failed to initialize application " << AppTitle;
		return -1;
	}

	ShowWindow(wnd, nShowCmd);
	UpdateWindow(wnd);

	AppTitle = g_pEngine->GetAppTitle();

	Timer timer;
	double prevTime = timer.GetElapsedTime();
	double filteredFrameTime = 0.0;

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
			double CurrTime = timer.GetElapsedTime();
			double ElapsedTime = CurrTime - prevTime;
			prevTime = CurrTime;

			if (g_pEngine->IsReady())
			{
				g_pEngine->Update(CurrTime, ElapsedTime);
				g_pEngine->Render();
				g_pEngine->Present();

				double filterScale = 0.2;
				filteredFrameTime = filteredFrameTime * (1.0 - filterScale) + filterScale * ElapsedTime;

				std::stringstream fpsCounterSS;
				fpsCounterSS << AppTitle << " - " << std::fixed << std::setprecision(1) << filteredFrameTime * 1000;
				fpsCounterSS << " ms (" << 1.0 / filteredFrameTime << " fps)";
				SetWindowTextA(wnd, fpsCounterSS.str().c_str());
			}
		}
	}

	g_pEngine.reset();
	return (int)msg.wParam;
}

LRESULT CALLBACK MessageProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (g_pEngine)
	{
		LRESULT res = g_pEngine->HandleWin32Message(wnd, message, wParam, lParam);
		if (res != 0) return res;
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
	case WM_SIZE:
		if (g_pEngine)
		{
			g_pEngine->WindowResize(LOWORD(lParam), HIWORD(lParam));
		}
		return 0;

	case WM_CHAR:
		if (wParam == VK_ESCAPE && (g_pEngine->GetHotKeyFlags() & HOT_KEY_FLAG_ALLOW_EXIT_ON_ESC))
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
