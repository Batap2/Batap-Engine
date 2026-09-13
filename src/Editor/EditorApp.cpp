#include "EditorApp.h"

#include "App.h"
#include "Platform/PlatformWindow.h"
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

        // `--game <dll>` (dev mode): the game is loaded as a module instead of
        // coming from the opened project.
        const auto args = platformCommandLineArgs();
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "--game" && app.gameModule_.load(args[i + 1]))
                app.game_ = app.gameModule_.makeGame();

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
