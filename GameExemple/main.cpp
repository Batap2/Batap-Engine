#include "Engine.h"
#include "InputManager.h"
#include "World.h"

#include <imgui.h>

#include <iostream>

int main()
{
    batap::Engine engine{{.title = "Batap TestGame",
                          .width = 1280,
                          .height = 720,
                          .fpsInTitle = true,
                          .transparent = true}};

    batap::World world{engine};
    world.loadScene("scenes/Cornel/cornelScene.btpl");

    while (batap::Frame frame = engine.nextFrame())
    {
        if (frame.input().pressed(batap::Key::Space))
            std::cout << "oep\n";

        ImGui::ShowDemoWindow();

        world.update();
    }

    return 0;
}
