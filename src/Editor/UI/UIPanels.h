#pragma once

#include "ColliderHandles.h"
#include "Components/EntityHandle.h"
#include "EigenTypes.h"
#include "Gizmo.h"
#include "LightHandles.h"
#include "Selection.h"
#include "Shortcuts.h"

#include <imgui.h>
#include "UI/ContentBrowser.h"
#include "UI/InspectorPanel.h"
#include "UI/MaterialEditorPanel.h"
#include "UI/ScenePanel.h"

#include <optional>
#include <string>
#include <string_view>

namespace batap
{
struct World;
struct App;
struct Engine;

struct UIPanels
{
    void draw(World& world, App& app, Engine& ctx);
    void drawStartupScreen(App& app, Engine& ctx);
    void clearSelection()
    {
        selection_.clear();
        colliderHandles_.clear();
    }
    void select(EntityHandle ent) { selection_.set(ent); }
    void addToSelection(EntityHandle ent) { selection_.add(ent); }
    void openMaterialEditor(MaterialHandle mat) { materialEditor_.open(mat); }
    void selectByName(World& world, std::string_view name);
    void setLogo(ImTextureID logo) { logo_ = logo; }
    ColliderHandles& colliderHandles() { return colliderHandles_; }
    ContentBrowser& contentBrowser() { return contentBrowser_; }
    Shortcuts& shortcuts() { return shortcuts_; }
    void requestTemplate(App& app, std::string path);

   private:
    void pickOnClick(World& world, App& app, Engine& ctx);
    void dropContentOnView(World& world, App& app, Engine& ctx, ImVec2 viewMin, ImVec2 viewMax);
    void drawTemplateClose(App& app);
    void drawSelectionBounds(World& world, App& app, Engine& ctx);
    void deleteSelection(World& world, App& app);
    void duplicateSelection(World& world, App& app);
    void handleShortcuts(World& world, App& app, Engine& ctx, bool handlesIdle);
    void save(World& world, App& app, bool askPath);
    void openScene(App& app);
    void drawTopBar(App& app, Engine& ctx);
    void drawRail(World& world, App& app, Engine& ctx, float top, float height);
    void drawWindowButtons(Engine& ctx, float height);
    void drawSettingsWindow(App& app, Engine& ctx);
    void loadAppSettings(App& app, Engine& ctx);
    void drawGeneralSettings(App& app, Engine& ctx);
    void drawShortcutSettings(Engine& ctx);

    // The window and render sections of each application's user file, as the
    // settings menu last read or wrote them. Reloaded each time the menu opens.
    struct AppSettings
    {
        int screen = 0;
        bool focusOnShow = true;
        bool behindOnShow = false;
        bool rayTracing = true;
        int maxFps = 0;
        int giBounces = 2;
        int giPasses = 2;
        std::string tonemap = "agx";
        bool giPerCell = false;
    };
    AppSettings editorSettings_;
    AppSettings gameSettings_;
    void drawFileMenu(World& world, App& app);
    void drawImportMenu(World& world, App& app, Engine& ctx);
    void drawViewMenu(World& world, App& app);
    void drawEditorOptions(World& world, App& app);
    void drawCameraOptions(World& world, App& app);

    static constexpr float kRailWidth = 40.0f;
    // The bar is as tall as the rail is wide, so the logo squares up with it.
    static constexpr float kTopBarHeight = kRailWidth;
    static constexpr float kWindowButtonsWidth = 108.0f;
    static constexpr float kRailInset = 9.0f;
    static constexpr float kRailIconSize = kRailWidth - kRailInset * 2.0f;

    float outlinerWidth_ = 230.0f;
    float inspectorWidth_ = 300.0f;
    float optionsHeight_ = 300.0f;
    bool railOpen_ = true;
    bool closeTemplatePending_ = false;
    bool settingsOpen_ = false;
    std::optional<Shortcut> capturing_;
    std::string nextTemplate_;
    ImTextureID logo_ = 0;
    quatf cameraFrameSource_ = quatf::Identity();
    v3f cameraFrameEulerDeg_ = v3f::Zero();

    Selection selection_;
    Gizmo gizmo_;
    LightHandles lightHandles_;
    ColliderHandles colliderHandles_;
    ContentBrowser contentBrowser_;
    ScenePanel scenePanel_;
    InspectorPanel inspectorPanel_;
    MaterialEditorPanel materialEditor_;
    Shortcuts shortcuts_;
};
}  // namespace batap
