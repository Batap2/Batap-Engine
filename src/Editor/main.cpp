// #define WIN32_LEAN_AND_MEAN

#include "WindowsApp.h"

using namespace batap;

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    RedirectIOToConsole();
    App app;
    return runWindowApp(hInstance, app);
}
