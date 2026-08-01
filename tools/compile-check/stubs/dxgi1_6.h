#pragma once
// compile-check stub — the real header comes from the Windows SDK.
// Engine headers only ever hold DXGI interfaces through ComPtr<> members, so
// forward declarations are enough to parse them. The .cpp files that actually
// call DXGI (Renderer.cpp & co) are Windows-only and excluded from the check.
// DXGI_FORMAT itself comes from DirectX-Headers (dxgiformat.h, via d3d12.h).

struct IDXGIAdapter1;
struct IDXGIDebug1;
struct IDXGIFactory4;
struct IDXGIFactory5;
struct IDXGISwapChain1;
struct IDXGISwapChain4;
