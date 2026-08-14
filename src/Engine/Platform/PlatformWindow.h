#pragma once

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

void platformShowWindow(void* nativeHandle);

void platformSetWindowTitle(void* nativeHandle, const std::string& title);

// PAS le handle de fenêtre (ça, c'est le retour de platformCreateWindow) :
// l'objet sur lequel le renderer crée sa surface d'affichage (VkSurfaceKHR).
// Windows : le HWND lui-même (les deux rôles coïncident).
// macOS   : la CAMetalLayer* posée dans la fenêtre (≠ la NSWindow).
void* platformSurfaceHandle(void* nativeHandle);

// Backend plateforme d'ImGui (imgui_impl_osx / imgui_impl_win32) — appelé par
// le renderer, qui possède le cycle de vie ImGui mais pas les types natifs.
void platformImGuiInit(void* nativeHandle);
void platformImGuiNewFrame(void* nativeHandle);
void platformImGuiShutdown();

// False once the window asked to close.
bool platformPumpMessages();

// Path resolution must never depend on the working directory: it changes
// with how the app is launched (double-click, terminal, debugger).
std::string platformExeDir();

// Program name excluded. Exists because main's argv is not reachable from
// a windowed-subsystem entry point.
std::vector<std::string> platformCommandLineArgs();
}  // namespace batap
