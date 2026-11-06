#include "App.h"

#include "Assets/AssetLoader.h"
#include "Assets/Texture.h"
#include "Bbox.h"
#include "Components/Billboard_C.h"
#include "Components/Camera_C.h"
#include "Components/DirectionalLight_C.h"
#include "Components/EditorOnly_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "EditorConfig.h"
#include "Engine.h"
#include "FileDialog.h"
#include "Importers/FileImporter.h"
#include "Paths.h"
#include "Platform/PlatformWindow.h"
#include "Renderer/Renderer.h"
#include "Serialization/EntitySerializer.h"
#include "Spatial/SpatialIndex.h"
#include "Systems/FreeCamController_S.h"
#include "Systems/Hierarchy_S.h"
#include "Systems/Physics_S.h"
#include "Systems/Systems.h"
#include "UI/FieldUI.h"
#include "UI/UIPanels.h"
#include "UI/UITheme.h"
#include "Utils/UIDGenerator.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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
constexpr float kFocusMinRadius = 1.0f;
constexpr float kTemplateCamYaw = 0.6f;
constexpr float kTemplateCamPitch = -0.45f;
constexpr float kTemplateSunYaw = 0.8f;
constexpr float kTemplateSunPitch = -0.9f;
// Daylight sky against the light's default 100 000 lux: a unit sky is black
// at that exposure.
constexpr float kTemplateSkyIntensity = 6000.f;

void extendWithSubtree(World& world, Engine& ctx, entt::entity e, AABB& box)
{
    const auto* bb = world.registry_.try_get<Billboard_C>(e);
    if (!bb || bb->sizeMode_ != Billboards::SizeMode::Screen)
        if (const auto b = entityBounds(world, ctx, e))
            box.extend(*b);

    for (entt::entity child : Hierarchy_S::children({&world.registry_, e}))
        extendWithSubtree(world, ctx, child, box);
}

auto camState(const v3f& pos, const FreeCamController_C& ctrl, const Camera_C& view)
{
    return std::tuple{pos.x(),
                      pos.y(),
                      pos.z(),
                      ctrl.yaw_,
                      ctrl.pitch_,
                      ctrl.moveSpeed_,
                      ctrl.boostSpeed_,
                      ctrl.mouseSensitivity_,
                      ctrl.frame_.x(),
                      ctrl.frame_.y(),
                      ctrl.frame_.z(),
                      ctrl.frame_.w(),
                      view.fov_,
                      view.znear_,
                      view.ev100_,
                      view.autoExposure_,
                      view.exposureCompensation_,
                      view.minEv100_,
                      view.maxEv100_,
                      view.meteringPercentile_,
                      view.meteringRange_};
}

editorConfig::Exposure exposureOf(const Camera_C& view)
{
    return {view.ev100_,    view.autoExposure_,       view.exposureCompensation_, view.minEv100_,
            view.maxEv100_, view.meteringPercentile_, view.meteringRange_};
}

void setExposure(Camera_C& view, const editorConfig::Exposure& exposure)
{
    view.ev100_ = exposure.ev100;
    view.autoExposure_ = exposure.autoExposure;
    view.exposureCompensation_ = exposure.compensation;
    view.minEv100_ = exposure.minEv100;
    view.maxEv100_ = exposure.maxEv100;
    view.meteringPercentile_ = exposure.meteringPercentile;
    view.meteringRange_ = exposure.meteringRange;
}

void setLook(Camera_C& view, const Camera_C& from)
{
    setExposure(view, exposureOf(from));
    view.bloomIntensity_ = from.bloomIntensity_;
    view.bloomRadius_ = from.bloomRadius_;
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

EntityHandle App::activeSceneCamera()
{
    auto& reg = world_->registry_;
    for (auto [e, camera] : reg.view<Camera_C>(entt::exclude<EditorOnly_C>).each())
        if (camera.active_)
            return EntityHandle{&reg, e};
    return EntityHandle{};
}

void App::focusEditorCamera(EntityHandle target)
{
    if (!target.valid())
        return;
    const auto* tc = target.try_get<Transform_C>();
    if (!tc)
        return;

    AABB box;
    extendWithSubtree(*world_, *ctx_, target.entity_, box);
    frameEditorCamera(box, tc->worldPos());
}

void App::frameEditorCamera(const AABB& box, const v3f& fallbackCentre)
{
    EntityHandle cam = editorCamera();
    if (!cam.valid())
        return;

    const v3f center = box.valid() ? box.center() : fallbackCentre;
    const float radius = box.valid() ? std::max(box.halfSize().norm(), kFocusMinRadius)
                                     : kFocusMinRadius;

    Camera_C& view = cam.get<Camera_C>();
    const float distance = radius / std::sin(view.fov_ * 0.5f);
    const v3f forward = cam.get<Transform_C>().rot() * -v3f::UnitZ();
    cam.setLocalPosition(center - forward * distance);
    if (view.ortho())
    {
        enterFreeCamOrtho(cam.get<FreeCamController_C>(), view, distance);
        world_->instances().markDirty<Camera_C>(cam);
    }
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
    view.orthoHeight_ = editorCamView_.orthoHeight_;
    setExposure(view, exposureOf(editorCamView_));
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
        Camera_C& view = cam.get<Camera_C>();

        // While a scene camera lends its look, the view shows it and the
        // editor's own stays as it was, for a scene without one.
        const EntityHandle scene = activeSceneCamera();
        Camera_C own = view;
        if (scene.valid())
            setLook(own, editorCamView_);

        // A template is looked at from wherever: its moves must not become
        // the scene's saved pose.
        if (!editingTemplate() && camState(tc.pos(), ctrl, own) !=
                                      camState(editorCamPos_, editorCamCtrl_, editorCamView_))
            markConfigDirty();

        editorCamPos_ = tc.pos();
        editorCamRot_ = tc.rot();
        editorCamCtrl_ = ctrl;
        editorCamCtrl_.requireRightMouseButton_ = true;
        editorCamView_.fov_ = view.fov_;
        editorCamView_.znear_ = view.znear_;
        editorCamView_.orthoHeight_ = view.orthoHeight_;
        setLook(editorCamView_, own);
        setLook(view, scene.valid() ? scene.get<Camera_C>() : editorCamView_);
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
    // MSVC deprecates getenv; these two reads only feed the TMP_* hook below.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const char* tmpImgui = std::getenv("TMP_IMGUI");
    const char* tmpWho = std::getenv("TMP_WHO");
#pragma clang diagnostic pop
    if (tmpImgui && world_ && state_ != AppState::SelectProject)
    {
        static int f = 0;
        ++f;
        ImGuiIO& io = ImGui::GetIO();
        const char* who = tmpWho ? tmpWho : "Buggy";
        if (f == 100)
            uiPanels_.selectByName(*world_, who);
        if (f == 150)
        {
            io.AddMousePosEvent(1094.f, 136.f);
            io.AddMouseButtonEvent(0, true);
        }
        if (f == 152)
            io.AddMouseButtonEvent(0, false);
        if (f == 154)
            io.AddMouseButtonEvent(0, true);
        if (f == 156)
            io.AddMouseButtonEvent(0, false);
        if (f >= 148 && f <= 175)
        {
            v3f pos = v3f::Zero();
            for (auto [e, n, tc] : world_->registry_.view<Name_C, Transform_C>().each())
                if (n.name_ == who)
                    pos = tc.pos();
            std::printf("TMP f%d active %u textInput %d pos %.6f %.6f %.6f\n", f,
                        ImGui::GetCurrentContext()->ActiveId, int(io.WantTextInput),
                        static_cast<double>(pos.x()), static_cast<double>(pos.y()),
                        static_cast<double>(pos.z()));
            std::fflush(stdout);
        }
    }
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
        if (editingTemplate())
            ensureTemplateRig();
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
        if (templateFocusPending_)
        {
            templateFocusPending_ = false;
            auto& reg = world_->registry_;
            AABB box;
            for (auto e : reg.storage<entt::entity>())
            {
                if (!reg.valid(e) || reg.all_of<EditorOnly_C>(e))
                    continue;
                const auto* hc = reg.try_get<Hierarchy_C>(e);
                if (!hc || hc->parent == entt::null)
                    extendWithSubtree(*world_, *ctx_, e, box);
            }
            frameEditorCamera(box, v3f::Zero());
        }
    }
}

void App::openTemplate(const std::string& path)
{
    if (editingTemplate())
    {
        showToast("Close the open template first");
        return;
    }
    if (!std::filesystem::exists(path))
    {
        showToast("No such file: " + path);
        return;
    }
    if (playing_)
        stopPlay();
    if (configDirty_)
        saveConfig();

    sceneAside_ = {EntitySerializer::toBuffer(*world_, *ctx_),
                   scenePath_,
                   editorCamPos_,
                   editorCamRot_,
                   editorCamCtrl_,
                   world_->systems().physics_->showColliders_};
    templatePath_ = path;

    world_->resetScene();
    EntitySerializer::clearSceneAndLoad(*world_, *ctx_, path);
    templateSaved_ = EntitySerializer::templateToBuffer(*world_, *ctx_);
    uiPanels_.clearSelection();
    world_->systems().physics_->showColliders_ = true;

    editorCamView_.orthoHeight_ = 0.f;
    editorCamCtrl_.frame_ = quatf::Identity();
    editorCamCtrl_.yaw_ = kTemplateCamYaw;
    editorCamCtrl_.pitch_ = kTemplateCamPitch;
    editorCamRot_ = freeCamRotation(editorCamCtrl_);
    templateFocusPending_ = true;
}

bool App::saveTemplate()
{
    return saveTemplateAs(templatePath_);
}

bool App::saveTemplateAs(std::string path)
{
    if (!editingTemplate() || path.empty())
        return false;
    if (!EntitySerializer::saveTemplate(*world_, *ctx_, path))
    {
        showToast("Could not write " + path);
        return false;
    }
    ctx_->assetManager_->saveAllAssets();
    templatePath_ = std::move(path);
    templateSaved_ = EntitySerializer::templateToBuffer(*world_, *ctx_);
    showToast("Template saved");
    return true;
}

void App::closeTemplate()
{
    if (!editingTemplate())
        return;
    if (playing_)
        stopPlay();

    templatePath_.clear();
    templateSaved_.clear();
    templateFocusPending_ = false;

    world_->resetScene();
    EntitySerializer::clearSceneAndLoadBuffer(*world_, *ctx_, sceneAside_.snapshot_);
    scenePath_ = sceneAside_.path_;
    editorCamPos_ = sceneAside_.camPos_;
    editorCamRot_ = sceneAside_.camRot_;
    editorCamCtrl_ = sceneAside_.camCtrl_;
    editorCamView_.orthoHeight_ = 0.f;
    world_->systems().physics_->showColliders_ = sceneAside_.showColliders_;
    sceneAside_ = {};
    uiPanels_.clearSelection();
}

bool App::templateDirty()
{
    return editingTemplate() &&
           EntitySerializer::templateToBuffer(*world_, *ctx_) != templateSaved_;
}

void App::ensureTemplateRig()
{
    auto& reg = world_->registry_;
    if (reg.view<DirectionalLight_C>().empty())
    {
        EntityHandle sun = world_->spawn("directionalLight");
        if (sun.valid())
        {
            reg.emplace<EditorOnly_C>(sun.entity_);
            sun.get<Name_C>().name_ = "template sun";
            sun.setLocalRotation((quatf{angleaxisf(kTemplateSunYaw, v3f::UnitY())} *
                                  quatf{angleaxisf(kTemplateSunPitch, v3f::UnitX())})
                                     .normalized());
        }
    }
    if (reg.view<Skybox_C>().empty())
    {
        EntityHandle sky = world_->spawn("skybox");
        if (sky.valid())
        {
            reg.emplace<EditorOnly_C>(sky.entity_);
            sky.get<Name_C>().name_ = "template sky";
            Skybox_C& skybox = sky.write<Skybox_C>();
            skybox.mode_ = Skybox_C::Mode::Gradient;
            skybox.color1_ = {0.42f, 0.52f, 0.66f};
            skybox.color2_ = {0.74f, 0.77f, 0.81f};
            skybox.color3_ = {0.28f, 0.28f, 0.30f};
            skybox.intensity_ = kTemplateSkyIntensity;
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
    playCamPos_ = editorCamPos_;
    playCamRot_ = editorCamRot_;
    playing_ = true;
    if (game_)
        game_->init(*world_);
}

void App::stopPlay()
{
    if (!playing_)
        return;
    playing_ = false;
    world_->resetScene();
    EntitySerializer::clearSceneAndLoadBuffer(*world_, *ctx_, playSnapshot_);
    playSnapshot_.clear();
    // The game's recentrings (World::shiftFrame) carried the editor camera
    // along with the frame; the snapshot has just put the frame back.
    editorCamPos_ = playCamPos_;
    editorCamRot_ = playCamRot_;
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

void App::revealInExplorer(const std::string& path)
{
#if defined(_WIN32)
    ::ShellExecuteA(nullptr, "open", "explorer.exe", ("/select,\"" + path + "\"").c_str(), nullptr,
                    SW_SHOWNORMAL);
#else
    spawnDetached("xdg-open", "\"" + std::filesystem::path(path).parent_path().string() + "\"");
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
    uiPanels_.contentBrowser().setView(file.browserGrid, file.browserTileSize);
    editorConfig::readShortcuts(uiPanels_.shortcuts());
}

void App::saveConfig()
{
    editorConfig::File file;
    file.theme = theme_;
    file.recent = recentProjects_;
    file.browserGrid = uiPanels_.contentBrowser().grid();
    file.browserTileSize = uiPanels_.contentBrowser().tileSize();
    file.camera = {editorCamCtrl_.moveSpeed_, editorCamCtrl_.boostSpeed_,
                   editorCamCtrl_.mouseSensitivity_, editorCamView_.fov_, editorCamView_.znear_};
    file.pose = {editorCamPos_,         editorCamCtrl_.yaw_,        editorCamCtrl_.pitch_,
                 editorCamCtrl_.frame_, exposureOf(editorCamView_), true};
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
    file.camera = {editorCamCtrl_.moveSpeed_, editorCamCtrl_.boostSpeed_,
                   editorCamCtrl_.mouseSensitivity_, editorCamView_.fov_, editorCamView_.znear_};
    editorConfig::read(file, projectDir_, {});

    editorCamCtrl_.moveSpeed_ = file.camera.moveSpeed;
    editorCamCtrl_.boostSpeed_ = file.camera.boostSpeed;
    editorCamCtrl_.mouseSensitivity_ = file.camera.mouseSensitivity;
    editorCamView_.fov_ = file.camera.fov;
    editorCamView_.znear_ = file.camera.znear;

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
    file.pose = {editorCamPos_, editorCamCtrl_.yaw_, editorCamCtrl_.pitch_, editorCamCtrl_.frame_,
                 exposureOf(editorCamView_)};
    editorConfig::read(file, projectDir_, scene);
    if (!file.hasPose)
        return;

    if (file.pose.hasExposure)
        setExposure(editorCamView_, file.pose.exposure);
    editorCamPos_ = file.pose.pos;
    editorCamCtrl_.yaw_ = file.pose.yaw;
    editorCamCtrl_.pitch_ = file.pose.pitch;
    editorCamCtrl_.frame_ = file.pose.frame;
    editorCamRot_ = freeCamRotation(editorCamCtrl_);
    editorCamView_.orthoHeight_ = 0.f;

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

void App::selectProject(const std::string& dir, bool openStartScene)
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

    const std::string start = editorConfig::readStartScene(dir);
    if (!openStartScene || start.empty() || editingTemplate())
        return;
    const std::string path = (std::filesystem::path(dir) / start).string();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        showToast("Start scene not found: " + start);
        return;
    }
    stopPlay();
    EntitySerializer::clearSceneAndLoad(*world_, *ctx_, path);
    setScenePath(path);
    uiPanels_.clearSelection();
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
