#include <windows.h>

#include "App.h"
#include "Engine.h"
#include "World.h"

int CALLBACK wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    batap::Engine engine{{.title = "Batap Engine", .width = 1280, .height = 720}};
    batap::World  world{engine};
    batap::App    app{engine, world};

    while (batap::Frame frame = engine.nextFrame())
        app.update();

    return 0;
}
