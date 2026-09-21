#include "App.h"

#include "Assets/AssetLoader.h"
#include "Assets/Texture.h"
#include "Components/Camera_C.h"
#include "Components/EditorOnly_C.h"
#include "Components/Transform_C.h"
#include "EditorConfig.h"
#include "Engine.h"
#include "FileDialog.h"
#include "Importers/FileImporter.h"
#include "Paths.h"
#include "Platform/PlatformWindow.h"
#include "Renderer/Renderer.h"
#include "Serialization/EntitySerializer.h"
#include "UI/FieldUI.h"
#include "UI/UIPanels.h"
#include "UI/UITheme.h"
#include "Utils/UIDGenerator.h"


#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <tuple>


#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace batap
{

namespace
{
constexpr auto kConfigSaveDelay = std::chrono::seconds(1);

quatf camRotation(float yaw, float pitch)
{
    return (quatf{angleaxisf(yaw, v3f::UnitY())} * quatf{angleaxisf(pitch, v3f::UnitX())})
        .normalized();
}

auto camState(const v3f& pos, const FreeCamController_C& ctrl, const Camera_C& view)
{
    return std::tuple{pos.x(),     pos.y(),         pos.z(),          ctrl.yaw_,
                      ctrl.pitch_, ctrl.moveSpeed_, ctrl.boostSpeed_, ctrl.mouseSensitivity_,
                      view.fov_,   view.znear_,     view.zfar_};
}
}  // namespace

App::App(Engine& engine, World& world)
    : ctx_(&engine), world_(&world), assetManager_(ctx_->assetManager_.get())
{
    loadConfig();
    ui::ApplyTheme(theme_);
    ui::smallFont = ctx_->renderer_->smallFont();
    ui::monoFont = ctx_->renderer_->monoFont();

    const std::string logoPath = resolveEngineFile("assets/logo.png", "assets/logo.png");
    if (auto logo = loadAsset<Texture>(logoPath, *ctx_))
        if (auto* tex = assetManager_->get<Texture>(logo))
            uiPanels_.setLogo(ctx_->renderer_->imguiTexture(tex->gpu_));
    installFieldUI();

    editorCamCtrl_.requireRightMouseButton_ = true;
}

EntityHandle App::editorCamera()
{
    auto& reg = world_->registry_;
    auto view = reg.view<EditorOnly_C, Camera_C>();
    const auto it = view.begin();
    return it == view.end() ? EntityHandle{} : EntityHandle{&reg, *it};
}

void App::applyEditorCamera()
{
    EntityHandle cam = editorCamera();
    if (!cam.valid())
        return;

    cam.get<FreeCamController_C>() = editorCamCtrl_;
    Camera_C& view = cam.get<Camera_C>();
    view.fov_ = editorCamView_.fov_;
    view.znear_ = editorCamView_.znear_;
    view.zfar_ = editorCamView_.zfar_;
    cam.setLocalPosition(editorCamPos_);
    cam.setLocalRotation(editorCamRot_);
    world_->instances().markDirty<Camera_C>(cam);
}

void App::syncEditorCamera()
{
    EntityHandle cam = editorCamera();

    if (!cam.valid())
    {
        cam = world_->spawn("camera");
        world_->registry_.emplace<EditorOnly_C>(cam.entity_);
        cam.emplace<FreeCamController_C>(editorCamCtrl_);
        applyEditorCamera();
    }
    else
    {
        const Transform_C& tc = cam.get<Transform_C>();
        const FreeCamController_C& ctrl = cam.get<FreeCamController_C>();
        const Camera_C& view = cam.get<Camera_C>();

        if (camState(tc.pos(), ctrl, view) !=
            camState(editorCamPos_, editorCamCtrl_, editorCamView_))
            markConfigDirty();

        editorCamPos_ = tc.pos();
        editorCamRot_ = tc.rot();
        editorCamCtrl_ = ctrl;
        editorCamCtrl_.requireRightMouseButton_ = true;
        editorCamView_.fov_ = view.fov_;
        editorCamView_.znear_ = view.znear_;
        editorCamView_.zfar_ = view.zfar_;
    }

    world_->setRenderCamera(playing_ ? entt::null : cam.entity_);
}

void App::setScenePath(std::string path)
{
    if (!scenePath_.empty())
        saveConfig();

    scenePath_ = std::move(path);
    loadSceneCamera();
}

// The World outlives the App, so its registry would otherwise be destroyed
// after gameModule_ has unloaded the DLL — and an entt pool created by DLL
// code destroys itself through DLL code.
App::~App()
{
    // Before resetScene: the camera the config wants is the live one.
    if (configDirty_)
        saveConfig();

    game_.reset();
    if (world_)
        world_->resetScene();
}

void App::update()
{
    if (themeDirty_)
    {
        ui::ApplyTheme(theme_);
        themeDirty_ = false;
    }
    pumpMsgFileDialog();
    pumpGameModuleReload();

    if (configDirty_ && std::chrono::steady_clock::now() >= configSaveAt_)
        saveConfig();

    if (state_ == AppState::SelectProject)
    {
        uiPanels_.drawStartupScreen(*this, *ctx_);
    }
    else
    {
        uiPanels_.draw(*world_, *this, *ctx_);
        syncEditorCamera();
        editorIcons_.draw(*world_, *ctx_);
        if (playing_ && game_)
            world_->update(*game_);
        else
        {
            if (game_)
                game_->editorUpdate(*world_);
            world_->update();
        }
    }
}

void App::pumpGameModuleReload()
{
    if (!gameModule_.stagePending())
        return;

    const std::string snapshot = EntitySerializer::toBuffer(*world_, *ctx_);
    game_.reset();
    world_->resetScene();

    try
    {
        if (gameModule_.swapStaged())
        {
            adoptGame();
            showToast("Game reloaded");
        }
        else
            showToast("Game reload failed: " + gameModule_.lastError());
    }
    catch (const std::exception& e)
    {
        showToast(std::string("Game reload failed: ") + e.what());
    }

    EntitySerializer::clearSceneAndLoadBuffer(*world_, *ctx_, snapshot);
    uiPanels_.clearSelection();
}

void App::adoptGame()
{
    game_ = gameModule_.makeGame();
    if (gameModule_.api_.gameExeName_)
        gameExeName_ = gameModule_.api_.gameExeName_;
}

void App::showToast(std::string msg)
{
    toast_ = std::move(msg);
    toastEnd_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
}

void App::startPlay()
{
    if (playing_)
        return;
    playSnapshot_ = EntitySerializer::toBuffer(*world_, *ctx_);
    playing_ = true;
    if (game_)
        game_->init(*world_);
}

void App::stopPlay()
{
    if (!playing_)
        return;
    playing_ = false;
    EntitySerializer::clearSceneAndLoadBuffer(*world_, *ctx_, playSnapshot_);
    playSnapshot_.clear();
    // Every EntityHandle from before the reload is dead.
    uiPanels_.clearSelection();
}

static void spawnDetached(const std::string& exe, const std::string& args)
{
#if defined(_WIN32)
    ::ShellExecuteA(nullptr, "open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    std::system(("\"" + exe + "\" " + args + " &").c_str());
#endif
}

void App::runStandalone()
{
    if (gameExeName_.empty())
        return;

    namespace fs = std::filesystem;
    const fs::path scene = fs::temp_directory_path() / "batap_run.btpl";
    EntitySerializer::save(*world_, *ctx_, scene.string());

    fs::path exe = fs::path(platformExeDir()) / gameExeName_;
#if defined(_WIN32)
    exe += ".exe";
#endif
    spawnDetached(exe.string(),
                  "--project \"" + projectDir_ + "\" --scene \"" + scene.string() + "\"");
}

std::string App::sceneKey() const
{
    return editorConfig::sceneKey(projectDir_, scenePath_);
}

void App::loadConfig()
{
    editorConfig::File file;
    editorConfig::read(file, {}, {});
    theme_ = file.theme;
    recentProjects_ = std::move(file.recent);
}

void App::saveConfig()
{
    editorConfig::File file;
    file.theme = theme_;
    file.recent = recentProjects_;
    file.camera = {editorCamCtrl_.moveSpeed_,        editorCamCtrl_.boostSpeed_,
                   editorCamCtrl_.mouseSensitivity_, editorCamView_.fov_,
                   editorCamView_.znear_,            editorCamView_.zfar_};
    file.pose = {editorCamPos_, editorCamCtrl_.yaw_, editorCamCtrl_.pitch_};
    file.hasPose = true;

    editorConfig::write(file, projectDir_, sceneKey());
    configDirty_ = false;
}

void App::loadProjectCamera()
{
    editorCamCtrl_ = FreeCamController_C{};
    editorCamCtrl_.requireRightMouseButton_ = true;
    editorCamView_ = Camera_C{};

    // Seeded with the defaults just restored: a key the file does not carry
    // must come back as the component wrote it, not as a zero.
    editorConfig::File file;
    file.camera = {editorCamCtrl_.moveSpeed_,        editorCamCtrl_.boostSpeed_,
                   editorCamCtrl_.mouseSensitivity_, editorCamView_.fov_,
                   editorCamView_.znear_,            editorCamView_.zfar_};
    editorConfig::read(file, projectDir_, {});

    editorCamCtrl_.moveSpeed_ = file.camera.moveSpeed;
    editorCamCtrl_.boostSpeed_ = file.camera.boostSpeed;
    editorCamCtrl_.mouseSensitivity_ = file.camera.mouseSensitivity;
    editorCamView_.fov_ = file.camera.fov;
    editorCamView_.znear_ = file.camera.znear;
    editorCamView_.zfar_ = file.camera.zfar;

    applyEditorCamera();
}

// A scene never opened before keeps the camera where it is, rather than
// throwing it back to the origin.
void App::loadSceneCamera()
{
    const std::string scene = sceneKey();
    if (scene.empty())
        return;

    editorConfig::File file;
    file.pose = {editorCamPos_, editorCamCtrl_.yaw_, editorCamCtrl_.pitch_};
    editorConfig::read(file, projectDir_, scene);
    if (!file.hasPose)
        return;

    editorCamPos_ = file.pose.pos;
    editorCamCtrl_.yaw_ = file.pose.yaw;
    editorCamCtrl_.pitch_ = file.pose.pitch;
    editorCamRot_ = camRotation(editorCamCtrl_.yaw_, editorCamCtrl_.pitch_);

    applyEditorCamera();
}

void App::markConfigDirty()
{
    configDirty_ = true;
    configSaveAt_ = std::chrono::steady_clock::now() + kConfigSaveDelay;
}

// PopStyleColor restores the value saved at push time, so a theme applied from
// inside a panel is undone when that panel pops. Applied at the next frame start.
void App::setTheme(ui::Theme theme)
{
    theme_ = theme;
    themeDirty_ = true;
    saveConfig();
}

void App::selectProject(const std::string& dir)
{
    if (!projectDir_.empty())
        saveConfig();

    // The scene that was open belongs to the project being left, so its name
    // must not become a key under the new one.
    scenePath_.clear();
    projectDir_ = dir;
    loadProjectCamera();
    ctx_->assetManager_->setBaseDir(dir);
    state_ = AppState::Running;

    if (!game_ && !gameModule_.loaded())
    {
        try
        {
            const auto dll = std::filesystem::path(dir) / "bin" / GameModuleFileName;
            if (!std::filesystem::exists(dll))
            {
                // A project with no game module is legitimate, but the name
                // differs per configuration — so name the one looked for.
                showToast("No " + std::string(GameModuleFileName) + " in " +
                          (std::filesystem::path(dir) / "bin").string());
            }
            else if (gameModule_.load(dll.string()))
            {
                adoptGame();
                showToast("Game loaded");
            }
            else
                showToast("Game load failed: " + gameModule_.lastError());
        }
        catch (const std::exception& e)
        {
            showToast(std::string("Game load failed: ") + e.what());
        }
    }

    recentProjects_.erase(std::remove(recentProjects_.begin(), recentProjects_.end(), dir),
                          recentProjects_.end());
    recentProjects_.insert(recentProjects_.begin(), dir);
    if (recentProjects_.size() > 10)
        recentProjects_.resize(10);
    saveConfig();
}

uint64_t App::openFileDialogAsyncWithAfterJob(std::span<const FileDialogFilter> filters,
                                              FileDialogAfterJob job)
{
    auto id = next_uid64();
    fileDialogAfterJobs_.emplace(id, std::move(job));
    OpenFilesDialogAsync(filters, &fileDialogMsgBus_, id);
    return id;
}

uint64_t App::openFolderDialogAsyncWithAfterJob(FileDialogAfterJob job)
{
    auto id = next_uid64();
    fileDialogAfterJobs_.emplace(id, std::move(job));
    OpenFolderDialogAsync(&fileDialogMsgBus_, id);
    return id;
}

void App::importAssets(std::span<const std::string> paths)
{
    size_t imported = 0;
    size_t reloaded = 0;
    std::string skipped;
    std::string failed;

    const auto note = [](std::string& list, const std::string& path)
    {
        if (!list.empty())
            list += ", ";
        list += std::filesystem::path(path).filename().string();
    };

    for (const std::string& path : paths)
    {
        const ImportResult result = importFile(path, ImportOptions{projectDir_});
        if (result.kind == ImportResult::Kind::Unsupported)
        {
            note(skipped, path);
            continue;
        }
        if (!result)
        {
            note(failed, path);
            continue;
        }
        ++imported;
        reloaded += reloadImportedAssets(result, *assetManager_);
    }

    std::string msg;
    if (imported > 0)
        msg = (reloaded == 0 ? "Imported " : "Reimported ") + std::to_string(imported) + " file(s)";
    if (reloaded > 0)
        msg += ", " + std::to_string(reloaded) + " asset(s) refreshed";
    if (!skipped.empty())
        msg += (msg.empty() ? "Format not imported: " : " — not imported: ") + skipped;
    if (!failed.empty())
        msg += (msg.empty() ? "Import failed: " : " — failed: ") + failed;
    if (!msg.empty())
        showToast(std::move(msg));
}

void App::pumpMsgFileDialog()
{
    fileDialogMsgBus_.pumpType<FileDialogMsg>(
        [&](FileDialogMsg&& msg)
        {
            auto it = fileDialogAfterJobs_.find(msg.id_);
            if (it != fileDialogAfterJobs_.end())
            {
                it->second(std::move(msg.paths_));
                fileDialogAfterJobs_.erase(it);
                return;
            }

            importAssets(msg.paths_);
        });
}

}  // namespace batap
