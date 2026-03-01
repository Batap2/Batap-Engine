#pragma once

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
  #pragma clang diagnostic ignored "-Werror"
#endif

#include <windows.h>
#include <wrl.h>

#include <DirectX-Headers/include/directx/d3d12.h>
#include <DirectX-Headers/include/directx/d3dx12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
