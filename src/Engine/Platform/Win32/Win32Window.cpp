#include "Platform/PlatformWindow.h"

#include "Engine.h"  // WindowDesc

#include <windows.h>

#include <shellapi.h>  // CommandLineToArgvW

#include <cassert>
#include <iostream>
#include <span>
#include <string>
#if defined(_DEBUG)
#include <cstdio>
#endif

#include "InputManager.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Renderer.h"

#include "imgui.h"

// Not declared by the backend header: the application is expected to forward
// window messages to it itself.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

// Optional: without this resource in the exe, the window keeps the default icon.
#define BATAP_ICON_RESOURCE_ID 101

namespace batap
{
namespace
{
LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // No Engine bound = engine still initialising, ImGui/InputManager don't
    // exist yet.
    auto* ctx = reinterpret_cast<Engine*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!ctx)
        return ::DefWindowProcW(hwnd, message, wParam, lParam);

    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
        return 1;

    ctx->_inputManager->ProcessWindowsEvent(message, wParam, lParam);

    switch (message)
    {
        case WM_INPUT:
            ctx->_inputManager->ProcessWindowsRawInput(lParam);
            break;

        // The default handler plays a notification sound on Alt+Enter.
        case WM_SYSCHAR:
            break;

        case WM_SIZE: {
            RECT client{};
            ::GetClientRect(hwnd, &client);
            ctx->_renderer->resize(static_cast<uint32_t>(client.right - client.left),
                                   static_cast<uint32_t>(client.bottom - client.top));
        }
        break;

        case WM_DESTROY:
        case WM_CLOSE:
            ::PostQuitMessage(0);
            break;

        default:
            return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return 0;
}

void registerWindowClass(HINSTANCE hInst, const wchar_t* className)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(WNDCLASSEXW);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc   = &wndProc;
    windowClass.hInstance     = hInst;
    windowClass.hIcon         = ::LoadIconW(nullptr, nullptr);
    // Unsuffixed: IDC_ARROW is an ANSI resource macro unless UNICODE is defined,
    // and the A/W distinction is meaningless for a stock system cursor.
    windowClass.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    windowClass.hIconSm       = ::LoadIconW(nullptr, nullptr);

    static ATOM atom = ::RegisterClassExW(&windowClass);
    assert(atom > 0 && "Failed to register window class");
}

void registerRawInputDevices(HWND hwnd)
{
    RAWINPUTDEVICE rid[2]{};

    rid[0].usUsagePage = 0x01;  // generic desktop
    rid[0].usUsage     = 0x02;  // mouse
    rid[0].dwFlags     = 0;
    rid[0].hwndTarget  = hwnd;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage     = 0x05;  // gamepad
    rid[1].dwFlags     = 0;
    rid[1].hwndTarget  = hwnd;

    if (!::RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
        std::cerr << "[Platform] Failed to register raw input devices.\n";
}

#if defined(_DEBUG)
void redirectIOToConsole()
{
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    std::ios::sync_with_stdio();
}
#endif
}  // namespace

void platformInit()
{
#if defined(_DEBUG)
    // Skip when a console is already attached (launched from a terminal).
    if (::GetConsoleWindow() == nullptr)
        redirectIOToConsole();
#endif

    // Per Monitor V2: the client area scales at 100% while non-client content
    // stays DPI aware.
    ::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void* platformCreateWindow(const WindowDesc& desc)
{
    HINSTANCE hInst = ::GetModuleHandleW(nullptr);

    const wchar_t* className = L"BatapWindow";
    registerWindowClass(hInst, className);

    RECT windowRect{0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height)};
    ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    const int windowWidth  = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;

    const int screenWidth  = ::GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);
    const int windowX      = std::max(0, (screenWidth - windowWidth) / 2);
    const int windowY      = std::max(0, (screenHeight - windowHeight) / 2);

    // WS_EX_NOREDIRECTIONBITMAP is required for the DirectComposition swap chain
    // (per-pixel window transparency) and can only be set at creation time.
    // Tied to CompositionSwapChain so the window and the swap chain cannot disagree.
    const DWORD exStyle = CompositionSwapChain ? WS_EX_NOREDIRECTIONBITMAP : 0u;

    const std::wstring wtitle(desc.title.begin(), desc.title.end());

    HWND hwnd = ::CreateWindowExW(exStyle, className, wtitle.c_str(), WS_OVERLAPPEDWINDOW, windowX,
                                  windowY, windowWidth, windowHeight, nullptr, nullptr, hInst,
                                  nullptr);
    assert(hwnd && "Failed to create window");
    if (!hwnd)
        return nullptr;

    if (HICON icon = ::LoadIconW(hInst, MAKEINTRESOURCEW(BATAP_ICON_RESOURCE_ID)))
    {
        ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }

    ::ShowCursor(TRUE);
    registerRawInputDevices(hwnd);

    return hwnd;
}

void platformBindContext(void* nativeHandle, Engine* ctx)
{
    ::SetWindowLongPtrW(static_cast<HWND>(nativeHandle), GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(ctx));
}

void platformShowWindow(void* nativeHandle)
{
    ::ShowWindow(static_cast<HWND>(nativeHandle), SW_SHOW);
}

void platformSetWindowTitle(void* nativeHandle, const std::string& title)
{
    const std::wstring wtitle(title.begin(), title.end());
    ::SetWindowTextW(static_cast<HWND>(nativeHandle), wtitle.c_str());
}

bool platformPumpMessages()
{
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return false;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return true;
}

std::string platformExeDir()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    std::wstring wpath(buffer, len);
    const size_t slash = wpath.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        wpath.resize(slash);

    // Exe paths are expected to be ASCII in this project; a lossless
    // conversion would go through WideCharToMultiByte.
    return std::string(wpath.begin(), wpath.end());
}

std::vector<std::string> platformCommandLineArgs()
{
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv)
        return {};
    if (argc < 1)
    {
        ::LocalFree(argv);
        return {};
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
    const auto argSpan = std::span<wchar_t*>(argv, static_cast<size_t>(argc));
#pragma clang diagnostic pop

    std::vector<std::string> args;
    args.reserve(argSpan.size() > 0 ? argSpan.size() - 1 : 0);
    for (const auto* warg : argSpan.subspan(1))
    {
        const std::wstring w{warg};
        args.emplace_back(w.begin(), w.end());
    }

    ::LocalFree(argv);
    return args;
}
}  // namespace batap
