#pragma once
// compile-check prelude — force-included before every TU (see check.sh).
// Layers the engine's few extra Windows needs on top of DirectX-Headers'
// official non-Windows adapter.

// DirectX-Headers stubs HWND as int, but the engine treats it as a pointer
// (`HWND _hwnd = nullptr`, static_cast from void*). Rename theirs out of the
// way and provide the pointer form.
#define HWND DXHEADERS_STUB_HWND
#include <wsl/winadapter.h>
#undef HWND
typedef void* HWND;

// Win32 event API called inline by FenceManager.h. Declarations only — the
// check never links.
extern "C"
{
    HANDLE CreateEvent(void* attrs, BOOL manualReset, BOOL initialState, const void* name);
    BOOL CloseHandle(HANDLE h);
    DWORD WaitForSingleObject(HANDLE h, DWORD ms);
}
#define INFINITE 0xFFFFFFFFu

// MSVC CRT helper used by the editor (App.cpp).
extern "C" int _dupenv_s(char** out, size_t* len, const char* name);
