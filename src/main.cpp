#define WIN32_LEAN_AND_MEAN

#include "Renderer/Renderer.h"
#include "App.h"
#include "Context.h"

#include <iostream>
#include <string>

using namespace RayVox;



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