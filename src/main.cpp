// #define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>

#include "App.h"
#include "Context.h"
#include "Renderer/Renderer.h"

using namespace batap;

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    RedirectIOToConsole();

    Context Ctx;
    InitApp(hInstance, Ctx);

    MSG msg = {};

    while (msg.message != WM_QUIT)
    {
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        Ctx.update();
    }

    Ctx._renderer->flush();

    return 0;
}
