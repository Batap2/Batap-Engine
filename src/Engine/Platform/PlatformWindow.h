#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace batap
{
struct Engine;
struct WindowDesc;

// Debug console + DPI awareness. Call once, before creating the window.
void platformInit();

// Returns the native handle (HWND) as an opaque pointer, null on failure.
// The window stays hidden until platformShowWindow.
void* platformCreateWindow(const WindowDesc& desc);

// The message procedure forwards to ImGui and the InputManager as soon as an
// Engine is bound — only bind once the engine is fully initialised.
void platformBindContext(void* nativeHandle, Engine* engine);

// activate false shows the window without taking the focus (WindowDesc).
void platformShowWindow(void* nativeHandle, bool activate);

// A monitor as the OS lists it, the primary one first so that index 0 means
// the same thing on every machine. `work` is the part a window may use (no
// taskbar). Pixels on Windows, points on macOS.
struct MonitorInfo
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int workX = 0;
    int workY = 0;
    int workWidth = 0;
    int workHeight = 0;
    bool primary = false;
};
std::vector<MonitorInfo> platformMonitors();

// Centres the window on that monitor, same size, without activating it; an
// index out of range means the primary one.
void platformMoveWindowToMonitor(void* nativeHandle, int screen);

void platformSetWindowTitle(void* nativeHandle, const std::string& title);

// The native object a VkSurfaceKHR is built from — not the surface itself,
// and not always the window handle. Windows: the HWND, both roles coincide.
// macOS: the contentView's CAMetalLayer, not the NSWindow.
void* platformSurfaceHandle(void* nativeHandle);

// Backend plateforme d'ImGui (imgui_impl_osx / imgui_impl_win32) — appelé par
// le renderer, qui possède le cycle de vie ImGui mais pas les types natifs.
void platformImGuiInit(void* nativeHandle);
void platformImGuiNewFrame(void* nativeHandle);
void platformImGuiShutdown();

// Same client-space pixels InputManager reports.
void platformSetCursorPos(void* nativeHandle, int clientX, int clientY);
void platformShowCursor(bool show);

void platformMinimizeWindow(void* nativeHandle);
void platformToggleMaximizeWindow(void* nativeHandle);
void platformCloseWindow(void* nativeHandle);
bool platformIsWindowMaximized(void* nativeHandle);

// False once the window asked to close.
bool platformPumpMessages();

// Precise: a plain sleep rounds to the scheduler tick (15.6 ms on Windows).
void platformSleepUntil(std::chrono::steady_clock::time_point target);

// Path resolution must never depend on the working directory: it changes
// with how the app is launched (double-click, terminal, debugger).
std::string platformExeDir();

// Program name excluded. Exists because main's argv is not reachable from
// a windowed-subsystem entry point.
std::vector<std::string> platformCommandLineArgs();
}  // namespace batap
