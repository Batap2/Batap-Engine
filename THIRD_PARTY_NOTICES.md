# Third-party code

## Bend Studio screen-space shadows

`src/Engine/Shaders/ScreenShadowCS.hlsl` and the dispatch planning in
`src/Engine/Renderer/Features/ScreenShadows/ScreenShadows.cpp` are ports of
`bend_sss_gpu.h` and `bend_sss_cpu.h` from Bend Studio's screen-space shadows.

Copyright 2023 Sony Interactive Entertainment.
Licensed under the Apache License, Version 2.0
(http://www.apache.org/licenses/LICENSE-2.0). Distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.

Changes from the original: reversed infinite depth with a far value of 0; the
surface thickness is 0.5 m along the view ray instead of a fixed depth ratio;
bilinear sampling-offset mode; depth reads with `Load` and an explicit border
instead of a border sampler; the early-out skips sky pixels and pixels off
screen; light coordinate and wave offsets are passed through `FrameGPUData`.
