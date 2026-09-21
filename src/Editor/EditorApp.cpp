#include "EditorApp.h"

#include "App.h"
#include "Platform/PlatformWindow.h"
#include "Serialization/EntitySerializer.h"
#include "World.h"

#include <exception>
#include <iostream>

namespace batap
{
int runEditor(const EditorConfig& cfg)
{
    try
    {
        Engine engine{cfg.window_};
        World world{engine};
        App app{engine, world};

        engine.setMaxFps(144);

        // `--game <dll>` (dev mode): the game is loaded as a module instead of
        // coming from the opened project.
        const auto args = platformCommandLineArgs();
        for (size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == "--game" && app.gameModule_.load(args[i + 1]))
                app.game_ = app.gameModule_.makeGame();
            else if (args[i] == "--project")
                app.selectProject(args[i + 1]);
            else if (args[i] == "--scene")
            {
                EntitySerializer::clearSceneAndLoad(world, engine, args[i + 1]);
                app.setScenePath(args[i + 1]);
            }
            else if (args[i] == "--select")
                app.uiPanels_.selectByName(world, args[i + 1]);
        }

        while (Frame frame = engine.nextFrame())
            app.update();

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
    catch (...)
    {
        std::cerr << "[FATAL] unknown exception\n";
        return 1;
    }
}
}  // namespace batap
