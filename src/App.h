#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h> // For CommandLineToArgvW
// Windows Runtime Library. Needed for Microsoft::WRL::ComPtr<> template class.
#include <wrl.h>
using namespace Microsoft::WRL;

#include <cstdint>

#include "Context.h"

#include "imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx12.h"

namespace RayVox
{
    inline HWND hWnd;
    inline RECT windowRect;
    inline uint32_t clientWidth = 1024;
    inline uint32_t clientHeight = 1024;

    inline bool AppInitialized = false;
    inline Context Ctx;

    inline bool isWindowFocused = false;

    void RedirectIOToConsole();

    HWND createWindow(const wchar_t* windowClassName, HINSTANCE hInst,
                      const wchar_t* windowTitle, uint32_t width, uint32_t height);

    void Resize(uint32_t width, uint32_t height);

    void SetFullscreen(bool fullscreen);

    LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void RegisterWindowClass( HINSTANCE hInst, const wchar_t* windowClassName );

    void RegisterRawInputDevices(HWND hwnd);

    void InitApp(HINSTANCE hInstance);
}