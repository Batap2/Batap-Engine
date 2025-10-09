#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>

#include "App.h"
#include "Context.h"
#include "Renderer/Renderer.h"

using namespace rayvox;

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    RedirectIOToConsole();
    InitApp(hInstance);

    MSG msg = {};

    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        Ctx.Update();
    }

    Ctx.renderer->flush();

    return 0;
}