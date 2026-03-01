#pragma once

#include <windows.h>

#include <shellapi.h>  // For CommandLineToArgvW

// Windows Runtime Library. Needed for Microsoft::WRL::ComPtr<> template class.
#include <wrl.h>

#include "Context.h"
#include "App.h"

#include <cstdint>

namespace batap
{
inline HWND hWnd;
inline RECT windowRect;
inline uint32_t clientWidth = 1280;
inline uint32_t clientHeight = 720;

inline bool AppInitialized = false;

inline bool isWindowFocused = false;

void RedirectIOToConsole();

HWND createWindow(const wchar_t* windowClassName, HINSTANCE hInst, const wchar_t* windowTitle,
                  uint32_t width, uint32_t height);

void Resize(uint32_t width, uint32_t height);

void SetFullscreen(bool fullscreen);

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

void RegisterWindowClass(HINSTANCE hInst, const wchar_t* windowClassName);

void RegisterRawInputDevices(HWND hwnd);

void InitApp(HINSTANCE hInstance, Context& ctx);

int runWindowApp(HINSTANCE hInstance, App& app);
}  // namespace batap
