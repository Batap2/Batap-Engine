#include "EditorApp.h"

#include "App.h"
#include "EditorConfig.h"
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
        // The editor's user file has the last word on where its window opens
        // and whether it takes the focus.
        WindowDesc window = cfg.window_;
        editorConfig::readWindow(window);
        Engine engine{window};
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
        for (const auto& arg : args)
            if (arg == "--play")
                app.startPlay();

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
