#include "InputManager.h"
#include <Windowsx.h>

void InputManager::manageInput(UINT message, WPARAM wParam, LPARAM lParam)
{

    switch (message)
    {
        case WM_KEYDOWN:
            switch (wParam) {
                case 'D': holdedKey[Right] = true; break;
                case 'Q': holdedKey[Left] = true; break;
                case 'Z': holdedKey[Forward] = true; break;
                case 'S': holdedKey[Backward] = true; break;
                case 'A': holdedKey[Down] = true; break;
                case 'E': holdedKey[Up] = true; break;
            }
            break;

        case WM_KEYUP:
            switch (wParam) {
                case 'D': holdedKey[Right] = false; break;
                case 'Q': holdedKey[Left] = false; break;
                case 'Z': holdedKey[Forward] = false; break;
                case 'S': holdedKey[Backward] = false; break;
                case 'A': holdedKey[Down] = false; break;
                case 'E': holdedKey[Up] = false; break;
            }
            break;

        case WM_MOUSEWHEEL:
        {
            if(GET_WHEEL_DELTA_WPARAM(wParam) < 0){
                ctx->camera.speed *= 0.92f;
            } else {
                ctx->camera.speed *= 1.08f;
            }
            break;
        }

        default:
            break;
    }

}

void InputManager::ProcessRawInput(LPARAM lParam)
{
    auto hRawInput = (HRAWINPUT)lParam;
    RAWINPUT raw;
    UINT size;

    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

    if (GetRawInputData(hRawInput, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == size)
    {
        if (raw.header.dwType == RIM_TYPEMOUSE)
        {
            // Traiter les données de la souris
            float mouseDeltaX = raw.data.mouse.lLastX * 0.001f;
            float mouseDeltaY = raw.data.mouse.lLastY * 0.001f;
            
            
            ctx->camera.rotate({0,1,0}, mouseDeltaX);
            ctx->camera.rotate(ctx->camera.getRightVec(), mouseDeltaY);
        }
    }
}

void InputManager::processTickInput()
{
    if (holdedKey[Right])
    {
        ctx->camera.move(ctx->camera.getRightVec());
    }
    if (holdedKey[Left])
    {
        ctx->camera.move(-ctx->camera.getRightVec());
    }
    if (holdedKey[Forward])
    {
        ctx->camera.move(ctx->camera.getForwardVec());
    }
    if (holdedKey[Backward])
    {
        ctx->camera.move(-ctx->camera.getForwardVec());
    }
    if (holdedKey[Down])
    {
        ctx->camera.move({0,-1,0});
    }
    if (holdedKey[Up])
    {
        ctx->camera.move({0,1,0});
    }
}
