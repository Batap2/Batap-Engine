#include "Platform/PlatformWindow.h"

#include "Engine.h"  // WindowDesc

#include <windows.h>

#include <dwmapi.h>    // DwmEnableBlurBehindWindow (fenêtre transparente)
#include <shellapi.h>  // CommandLineToArgvW
#include <windowsx.h>  // GET_X_LPARAM

#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#if defined(_DEBUG)
#include <cstdio>
#endif

#include "InputManager.h"
#include "Renderer/Renderer.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

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
// ---- Décodage Windows (WM_* / RAWINPUT) → InputManager::feed(), le miroir
// du décodage NSEvent de Platform/MacOS/MacOSWindow.mm. ----
const std::array<Key, 256> VkToKey = []
{
    std::array<Key, 256> t{};
    t.fill(Key::Unknown);

    t['A'] = Key::A;
    t['B'] = Key::B;
    t['C'] = Key::C;
    t['D'] = Key::D;
    t['E'] = Key::E;
    t['F'] = Key::F;
    t['G'] = Key::G;
    t['H'] = Key::H;
    t['I'] = Key::I;
    t['J'] = Key::J;
    t['K'] = Key::K;
    t['L'] = Key::L;
    t['M'] = Key::M;
    t['N'] = Key::N;
    t['O'] = Key::O;
    t['P'] = Key::P;
    t['Q'] = Key::Q;
    t['R'] = Key::R;
    t['S'] = Key::S;
    t['T'] = Key::T;
    t['U'] = Key::U;
    t['V'] = Key::V;
    t['W'] = Key::W;
    t['X'] = Key::X;
    t['Y'] = Key::Y;
    t['Z'] = Key::Z;

    t['0'] = Key::Num0;
    t['1'] = Key::Num1;
    t['2'] = Key::Num2;
    t['3'] = Key::Num3;
    t['4'] = Key::Num4;
    t['5'] = Key::Num5;
    t['6'] = Key::Num6;
    t['7'] = Key::Num7;
    t['8'] = Key::Num8;
    t['9'] = Key::Num9;

    t[VK_F1] = Key::F1;
    t[VK_F2] = Key::F2;
    t[VK_F3] = Key::F3;
    t[VK_F4] = Key::F4;
    t[VK_F5] = Key::F5;
    t[VK_F6] = Key::F6;
    t[VK_F7] = Key::F7;
    t[VK_F8] = Key::F8;
    t[VK_F9] = Key::F9;
    t[VK_F10] = Key::F10;
    t[VK_F11] = Key::F11;
    t[VK_F12] = Key::F12;
    t[VK_F13] = Key::F13;
    t[VK_F14] = Key::F14;
    t[VK_F15] = Key::F15;
    t[VK_F16] = Key::F16;
    t[VK_F17] = Key::F17;
    t[VK_F18] = Key::F18;
    t[VK_F19] = Key::F19;
    t[VK_F20] = Key::F20;
    t[VK_F21] = Key::F21;
    t[VK_F22] = Key::F22;
    t[VK_F23] = Key::F23;
    t[VK_F24] = Key::F24;

    t[VK_SHIFT] = Key::LShift;
    t[VK_CONTROL] = Key::LCtrl;
    t[VK_MENU] = Key::LAlt;  // Alt
    t[VK_LWIN] = Key::LSuper;
    t[VK_RWIN] = Key::RSuper;

    t[VK_ESCAPE] = Key::Escape;
    t[VK_RETURN] = Key::Enter;
    t[VK_SPACE] = Key::Space;
    t[VK_TAB] = Key::Tab;
    t[VK_BACK] = Key::Backspace;

    t[VK_INSERT] = Key::Insert;
    t[VK_DELETE] = Key::Delete;
    t[VK_HOME] = Key::Home;
    t[VK_END] = Key::End;
    t[VK_PRIOR] = Key::PageUp;
    t[VK_NEXT] = Key::PageDown;

    t[VK_UP] = Key::ArrowUp;
    t[VK_DOWN] = Key::ArrowDown;
    t[VK_LEFT] = Key::ArrowLeft;
    t[VK_RIGHT] = Key::ArrowRight;

    t[VK_OEM_MINUS] = Key::Minus;
    t[VK_OEM_PLUS] = Key::Equal;
    t[VK_OEM_4] = Key::LeftBracket;
    t[VK_OEM_6] = Key::RightBracket;
    t[VK_OEM_5] = Key::Backslash;
    t[VK_OEM_1] = Key::Semicolon;
    t[VK_OEM_7] = Key::Apostrophe;
    t[VK_OEM_3] = Key::Grave;
    t[VK_OEM_COMMA] = Key::Comma;
    t[VK_OEM_PERIOD] = Key::Period;
    t[VK_OEM_2] = Key::Slash;

    t[VK_NUMPAD0] = Key::Numpad0;
    t[VK_NUMPAD1] = Key::Numpad1;
    t[VK_NUMPAD2] = Key::Numpad2;
    t[VK_NUMPAD3] = Key::Numpad3;
    t[VK_NUMPAD4] = Key::Numpad4;
    t[VK_NUMPAD5] = Key::Numpad5;
    t[VK_NUMPAD6] = Key::Numpad6;
    t[VK_NUMPAD7] = Key::Numpad7;
    t[VK_NUMPAD8] = Key::Numpad8;
    t[VK_NUMPAD9] = Key::Numpad9;

    t[VK_ADD] = Key::NumpadAdd;
    t[VK_SUBTRACT] = Key::NumpadSubtract;
    t[VK_MULTIPLY] = Key::NumpadMultiply;
    t[VK_DIVIDE] = Key::NumpadDivide;
    t[VK_DECIMAL] = Key::NumpadDecimal;

    t[VK_CAPITAL] = Key::CapsLock;
    t[VK_NUMLOCK] = Key::NumLock;
    t[VK_SCROLL] = Key::ScrollLock;

    t[VK_SNAPSHOT] = Key::PrintScreen;
    t[VK_PAUSE] = Key::Pause;
    t[VK_APPS] = Key::Menu;

    return t;
}();

void decodeEvent(InputManager& input, UINT message, WPARAM wParam, LPARAM lParam)
{
    using KeyState = InputManager::KeyState;

    switch (message)
    {
        case WM_KEYDOWN:
            input.feed(InputManager::KeyEvent{KeyState::Pressed, VkToKey[wParam]});
            break;

        case WM_KEYUP:
            input.feed(InputManager::KeyEvent{KeyState::Released, VkToKey[wParam]});
            break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            const bool pressed = (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                                  message == WM_MBUTTONDOWN);
            InputManager::MouseEvent e{};
            e.Type = InputManager::MouseEvent::Type::Click;
            e.KeyState = pressed ? KeyState::Pressed : KeyState::Released;
            e.Button = (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
                           ? MouseButton::Left
                           : (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
                                 ? MouseButton::Right
                                 : MouseButton::Middle;
            e.ScreenPosition = input.MousePosition;
            input.feed(e);
            break;
        }

        case WM_MOUSEWHEEL: {
            InputManager::MouseEvent e{};
            e.Type = InputManager::MouseEvent::Type::Wheel;
            e.Wheel = GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f;
            e.ScreenPosition = input.MousePosition;
            input.feed(e);
            break;
        }

        case WM_MOUSEMOVE: {
            InputManager::MouseEvent e{};
            e.Type = InputManager::MouseEvent::Type::Move;
            // Le delta vient du RAWINPUT (WM_INPUT), pas de la position :
            // même découpage qu'avant la migration.
            e.Delta = {0, 0};
            e.ScreenPosition = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            input.feed(e);
            break;
        }
    }
}

void decodeRawInput(InputManager& input, LPARAM lParam)
{
    static constexpr UINT staticBufferSize = 256;
    static std::array<std::byte, staticBufferSize> staticBuffer;

    UINT bufferSize = staticBufferSize;
    ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, staticBuffer.data(),
                      &bufferSize, sizeof(RAWINPUTHEADER));

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(staticBuffer.data());
#pragma clang diagnostic pop
    if (raw->header.dwType != RIM_TYPEMOUSE)
        return;

    InputManager::MouseEvent e{};
    e.Type = InputManager::MouseEvent::Type::Move;
    e.Delta = {raw->data.mouse.lLastX, raw->data.mouse.lLastY};
    e.ScreenPosition = input.MousePosition;  // la position vient de WM_MOUSEMOVE
    input.feed(e);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // No Engine bound = engine still initialising, ImGui/InputManager don't
    // exist yet.
    auto* ctx = reinterpret_cast<Engine*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!ctx)
        return ::DefWindowProcW(hwnd, message, wParam, lParam);

    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
        return 1;

    decodeEvent(*ctx->inputManager_, message, wParam, lParam);

    switch (message)
    {
        case WM_INPUT:
            decodeRawInput(*ctx->inputManager_, lParam);
            break;

        // The default handler plays a notification sound on Alt+Enter.
        case WM_SYSCHAR:
            break;

        case WM_SIZE: {
            RECT client{};
            ::GetClientRect(hwnd, &client);
            ctx->renderer_->resize(static_cast<uint32_t>(client.right - client.left),
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
    // BLACK_BRUSH et pas COLOR_WINDOW : le bitmap de redirection DWM part
    // ainsi d'un alpha zéro, prérequis de la fenêtre transparente (et pour
    // une fenêtre opaque, un premier paint noir plutôt que blanc).
    windowClass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
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

    const std::wstring wtitle(desc.title.begin(), desc.title.end());

    HWND hwnd = ::CreateWindowExW(0, className, wtitle.c_str(), WS_OVERLAPPEDWINDOW, windowX,
                                  windowY, windowWidth, windowHeight, nullptr, nullptr, hInst,
                                  nullptr);
    assert(hwnd && "Failed to create window");
    if (!hwnd)
        return nullptr;

    if (desc.transparent)
    {
        // Dit à DWM de lire l'alpha du bitmap de redirection au compositing
        // (la région pleine ne floute rien depuis Win8, elle active l'alpha).
        // Côté swapchain : compositeAlpha PRE_MULTIPLIED si le driver l'expose.
        // Surtout PAS WS_EX_NOREDIRECTIONBITMAP : sans bitmap de redirection,
        // le driver Vulkan n'a nulle part où présenter.
        DWM_BLURBEHIND blur{};
        blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        blur.fEnable = TRUE;
        blur.hRgnBlur = ::CreateRectRgn(0, 0, -1, -1);  // toute la fenêtre
        ::DwmEnableBlurBehindWindow(hwnd, &blur);
        ::DeleteObject(blur.hRgnBlur);
    }

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

void* platformSurfaceHandle(void* nativeHandle)
{
    return nativeHandle;  // le HWND sert directement de surface
}

// Aujourd'hui le Renderer DX12 initialise ImGui_ImplWin32 lui-même ; ces
// hooks prennent le relais à l'étape C (backend Vulkan sous Windows), comme
// leurs équivalents Cocoa de MacOSWindow.mm.
void platformImGuiInit(void* nativeHandle)
{
    ImGui_ImplWin32_Init(nativeHandle);
}

void platformImGuiNewFrame(void* /*nativeHandle*/)
{
    ImGui_ImplWin32_NewFrame();
}

void platformImGuiShutdown()
{
    ImGui_ImplWin32_Shutdown();
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
