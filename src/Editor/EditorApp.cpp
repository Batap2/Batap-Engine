#include "EditorApp.h"

#include "App.h"
#include "EditorConfig.h"
#include "Importers/FileImporter.h"
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
        // The editor's user file has the last word on where its window opens,
        // whether it takes the focus and which renderer draws.
        WindowDesc window = cfg.window_;
        editorConfig::readWindow(window);
        EngineSettings settings{.maxFps_ = 144};
        editorConfig::readRender(settings);
        Engine engine{window, settings};
        World world{engine};
        App app{engine, world};

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
            else if (args[i] == "--import")
            {
                // Headless re-import for scripts: rewrites the engine files next
                // to the source (give --project first) and exits.
                const ImportResult result =
                    importFile(args[i + 1], ImportOptions{app.projectDir_});
                for (const std::string& file : result.writtenFiles)
                    std::cout << "[Import] " << file << "\n";
                if (!result)
                    std::cerr << "[Import] failed: " << args[i + 1] << " " << result.message
                              << "\n";
                return result ? 0 : 1;
            }
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
