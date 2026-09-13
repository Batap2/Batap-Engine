// Platform entry point. Compiled into the exe rather than Batap_EditorLib,
// which would drop it: nothing references its symbols.

#include "EditorApp.h"

#if defined(_WIN32)

#include <windows.h>

int CALLBACK wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return batap::runEditor();
}

#else

int main()
{
    return batap::runEditor();
}

#endif
