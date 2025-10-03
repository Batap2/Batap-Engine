#include "Renderer/Renderer.h"
#include "InputManager.h"
#include "backends/imgui_impl_dx12.h"

#include "App.h"

#include <iostream>
#include <algorithm>
#include <cassert>



#define MYICON 101
#pragma optimize("", off)
namespace RayVox
{
    HWND createWindow(const wchar_t* windowClassName, HINSTANCE hInst,
                      const wchar_t* windowTitle, uint32_t width, uint32_t height)
    {
        int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

        RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

        int windowWidth = windowRect.right - windowRect.left;
        int windowHeight = windowRect.bottom - windowRect.top;

        // Center the window within the screen. Clamp to 0, 0 for the top-left corner.
        int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
        int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);
        HWND hWnd = ::CreateWindowExW(
                NULL,
                windowClassName,
                windowTitle,
                WS_OVERLAPPEDWINDOW,
                windowX,
                windowY,
                windowWidth,
                windowHeight,
                NULL,
                NULL,
                hInst,
                nullptr
        );

        assert(hWnd && "Failed to create window");

        HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(MYICON));
        if (hIcon)
        {
            // Définir l'icône pour la fenêtre
            SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }

        return hWnd;
    }

    void Resize(uint32_t width, uint32_t height)
    {

//        if (clientWidth != width || clientHeight != height)
//        {
//            // Don't allow 0 size swap chain back buffers.
//            clientWidth = std::max(1u, width );
//            clientHeight = std::max(1u, height);
//
//            // Flush the GPU queue to make sure the swap chain's back buffers
//            // are not being referenced by an in-flight command list.
//
//            dx_cctx.directCommandQueue->Flush();
//            dx_cctx.computeCommandQueue->Flush();
//            dx_cctx.copyCommandQueue->Flush();
//
//            for (int i = 0; i < dx_cctx.bufferCount; ++i)
//            {
//                // Any references to the back buffers must be released
//                // before the swap chain can be resized.
//                dx_cctx.backBuffers[i].Reset();
//            }
//            DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
//            ThrowIfFailed(dx_cctx.swapChain->GetDesc(&swapChainDesc));
//            ThrowIfFailed(dx_cctx.swapChain->ResizeBuffers(dx_cctx.bufferCount, clientWidth, clientHeight,
//                                                           swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));
//
//            dx_cctx.currentBackBufferIndex = dx_cctx.swapChain->GetCurrentBackBufferIndex();
//
//            dx_cctx.UpdateRenderTargetViews(dx_cctx.device, dx_cctx.swapChain, dx_cctx.RTVDescriptorHeap);
//            dx_cctx.UpdateFrameBuffers(dx_cctx.device, dx_cctx.swapChain, dx_cctx.CBV_SRV_UAVDescriptorHeap);
//        }

    }

    void SetFullscreen(bool fullscreen)
    {
//        if (dx_cctx.fullscreen != fullscreen)
//        {
//            dx_cctx.fullscreen = fullscreen;
//
//            if (dx_cctx.fullscreen) // Switching to fullscreen.
//            {
//                // Store the current window dimensions so they can be restored
//                // when switching out of fullscreen state.
//                ::GetWindowRect(hWnd, &windowRect);
//                // Set the window style to a borderless window so the client area fills
//                // the entire screen.
//                UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
//
//                ::SetWindowLongW(hWnd, GWL_STYLE, windowStyle);
//                // Query the name of the nearest display device for the window.
//                // This is required to set the fullscreen dimensions of the window
//                // when using a multi-monitor setup.
//                HMONITOR hMonitor = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
//                MONITORINFOEX monitorInfo = {};
//                monitorInfo.cbSize = sizeof(MONITORINFOEX);
//                ::GetMonitorInfo(hMonitor, &monitorInfo);
//                ::SetWindowPos(hWnd, HWND_TOP,
//                               monitorInfo.rcMonitor.left,
//                               monitorInfo.rcMonitor.top,
//                               monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
//                               monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
//                               SWP_FRAMECHANGED | SWP_NOACTIVATE);
//
//                ::ShowWindow(hWnd, SW_MAXIMIZE);
//            }
//            else
//            {
//                // Restore all the window decorators.
//                ::SetWindowLong(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
//
//                ::SetWindowPos(hWnd, HWND_NOTOPMOST,
//                               windowRect.left,
//                               windowRect.top,
//                               windowRect.right - windowRect.left,
//                               windowRect.bottom - windowRect.top,
//                               SWP_FRAMECHANGED | SWP_NOACTIVATE);
//
//                ::ShowWindow(hWnd, SW_NORMAL);
//            }
//        }
    }

    // Window callback function.
    LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if ( AppInitialized )
        {
            Ctx.inputManager->ProcessWindowsEvent(message, wParam, lParam);
            int centerX = (windowRect.left + windowRect.right) / 2;
            int centerY = (windowRect.top + windowRect.bottom) / 2;

            switch (message)
            {
                case WM_INPUT: 
                    if(isWindowFocused)
                    {
                        //SetCursorPos(centerX, centerY);
                    }
                break;

                case WM_PAINT:
                    break;
                case WM_SYSKEYDOWN:
                case WM_KEYDOWN:
                {
                    bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

                    switch (wParam)
                    {
                        case VK_ESCAPE:
                            ::PostQuitMessage(0);
                            break;

                        case VK_RETURN:
                            if (alt)
                                SetFullscreen(!Ctx.renderer->fullscreen);
                            break;

                        case VK_F11:
                            SetFullscreen(!Ctx.renderer->fullscreen);
                            break;
                    }
                }
                break;
                // The default window procedure will play a system notification sound
                // when pressing the Alt+Enter keyboard combination if this message is
                // not handled.
                case WM_SYSCHAR:
                    break;
                case WM_SIZE:
                {
                    RECT clientRect = {};
                    ::GetClientRect(hWnd, &clientRect);

                    int width = clientRect.right - clientRect.left;
                    int height = clientRect.bottom - clientRect.top;

                    Resize(width, height);
                }
                break;
                case WM_SETFOCUS:
                    isWindowFocused = true;
                break;
                case WM_KILLFOCUS:
                    isWindowFocused = false;
                break;
                case WM_DESTROY:
                case WM_CLOSE:
                    ::PostQuitMessage(0);
                    break;
                default:
                    return ::DefWindowProcW(hwnd, message, wParam, lParam);
            }
        }
        else
        {
            return ::DefWindowProcW(hwnd, message, wParam, lParam);
        }

        return 0;
    }

    void RegisterWindowClass( HINSTANCE hInst, const wchar_t* windowClassName )
    {
        // Register a window class for creating our render window with.
        WNDCLASSEXW windowClass = {};

        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &WndProc;
        windowClass.cbClsExtra = 0;
        windowClass.cbWndExtra = 0;
        windowClass.hInstance = hInst;
        windowClass.hIcon = ::LoadIcon(hInst, NULL);
        windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
        windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        windowClass.lpszMenuName = NULL;
        windowClass.lpszClassName = windowClassName;
        windowClass.hIconSm = ::LoadIcon(hInst, NULL);

        static ATOM atom = ::RegisterClassExW(&windowClass);
        assert(atom > 0);
    }

    void RedirectIOToConsole()
    {
        // Allouer une console pour cette application.
        AllocConsole();

        // Rediriger la sortie standard vers la console.
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);

        // Optionnel: rediriger l'entrée standard de la console.
        freopen_s(&fp, "CONIN$", "r", stdin);

        // Synchroniser les flux de C++ avec ceux de C.
        std::ios::sync_with_stdio();
    }

    void RegisterRawInputDevices(HWND hwnd)
    {
        RAWINPUTDEVICE rid[1];

        // Enregistrer les données de la souris
        rid[0].usUsagePage = 0x01; // Page de périphérique générique
        rid[0].usUsage = 0x02;     // Usage : souris
        rid[0].dwFlags = RIDEV_INPUTSINK; // Recevoir les entrées même lorsque la fenêtre est inactive
        rid[0].hwndTarget = hwnd;

        if (!RegisterRawInputDevices(rid, 1, sizeof(rid[0])))
        {
            std::cerr << "Failed to register raw input devices." << std::endl;
        }
    }

    struct ImguiUserData{
        UINT heapIdx = 0;
    };
    void imguiSrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cH, D3D12_GPU_DESCRIPTOR_HANDLE* gH){
            auto res = Ctx.renderer->descriptorHeapAllocator_CBV_SRV_UAV.alloc();
            *cH = res->cpuHandle;
            *gH = res->gpuHandle;
            ImguiUserData* usrData = new ImguiUserData();
            usrData->heapIdx = res->heapIdx;
            info->UserData = usrData;
    };
    void imguiSrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cH, D3D12_GPU_DESCRIPTOR_HANDLE gH){
        if (!info->UserData) return;

        ImguiUserData* usrData = static_cast<ImguiUserData*>(info->UserData);
        Ctx.renderer->descriptorHeapAllocator_CBV_SRV_UAV.free(usrData->heapIdx);

        delete usrData;
        info->UserData = nullptr;
    };

    void InitApp(HINSTANCE hInstance)
    {

#if defined(_DEBUG)
        RedirectIOToConsole();
#endif

        // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
        // Using this awareness context allows the client area of the window
        // to achieve 100% scaling while still allowing non-client window content to
        // be rendered in a DPI sensitive fashion.
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        const wchar_t* windowClassName = L"RayVox-Engine";

        RegisterWindowClass(hInstance, windowClassName);
        hWnd = createWindow(windowClassName, hInstance, L"RayVox-Engine",
                            clientWidth, clientHeight);

        // Initialize the global window rect variable.
        ::GetWindowRect(hWnd, &windowRect);

        ShowCursor(FALSE);

        RegisterRawInputDevices(hWnd);

        Ctx.renderer->init(hWnd, clientWidth, clientHeight);

        ::ShowWindow(hWnd, SW_SHOW);
        
        //------- Init ImGui
        ImGui_ImplWin32_EnableDpiAwareness();
        float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
        
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup scaling
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
        style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

        // Setup Platform/Renderer backends
        ImGui_ImplWin32_Init(hWnd);

        ImGui_ImplDX12_InitInfo init_info = {};
        init_info.Device = Ctx.renderer->device.Get();
        init_info.CommandQueue = Ctx.renderer->CommandQueues[0].commandQueue.Get();
        init_info.NumFramesInFlight = Ctx.renderer->swapChain_buffer_count;
        init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
        // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
        // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
        init_info.SrvDescriptorHeap = Ctx.renderer->descriptorHeapAllocator_CBV_SRV_UAV.heap.Get();
        init_info.SrvDescriptorAllocFn = imguiSrvAlloc;
        init_info.SrvDescriptorFreeFn = imguiSrvFree;
        ImGui_ImplDX12_Init(&init_info);

        AppInitialized = true;
    }
}
#pragma optimize("", on)