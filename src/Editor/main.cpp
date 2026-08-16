#include "App.h"
#include "Engine.h"
#include "World.h"

namespace
{
int run()
{
    batap::Engine engine{{.title = "Batap Engine",
                          .width = 1280,
                          .height = 720,
                          .fpsInTitle = true,
                          .transparent = true}};
    batap::World  world{engine};
    batap::App    app{engine, world};

    while (batap::Frame frame = engine.nextFrame())
        app.update();

    return 0;
}
}  // namespace

#if defined(_WIN32)

#include <windows.h>

int CALLBACK wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return run();
}

#else

int main()
{
    return run();
}

#endif
