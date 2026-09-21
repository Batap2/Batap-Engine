#pragma once

#include "EigenTypes.h"
#include "UI/UITheme.h"

#include <string>
#include <vector>

namespace batap::editorConfig
{
// One spelling per path, or C:\Foo, C:/Foo and a relative argument key three
// different projects.
std::string pathKey(const std::string& path);

// Relative to its project, so a scene keys the same whether it came from
// --scene or from the file dialog, and the file stays readable.
std::string sceneKey(const std::string& projectDir, const std::string& scenePath);

struct Camera
{
    float moveSpeed = 0.f;
    float boostSpeed = 0.f;
    float mouseSensitivity = 0.f;
    float fov = 0.f;
    float znear = 0.f;
    float zfar = 0.f;
};

struct Pose
{
    v3f pos = v3f::Zero();
    float yaw = 0.f;
    float pitch = 0.f;
};

// One project and one of its scenes at a time, which is all the editor ever
// has open. The other projects and the other scenes live in the same file and
// are carried across a write untouched.
struct File
{
    ui::Theme theme = ui::Theme::Light;
    std::vector<std::string> recent;

    Camera camera;
    Pose pose;
    bool hasPose = false;
};

// A key the file does not carry leaves its field as it came in, so the
// defaults stay in the components instead of being spelled a second time
// here: seed `io` with the values in use. hasPose comes back saying whether
// the scene had an entry at all — one never opened must leave the camera
// where it is, not throw it back to the origin.
void read(File& io, const std::string& projectDir, const std::string& sceneKey);

// An empty projectDir writes theme and recents only; an empty sceneKey, or
// hasPose false, leaves the scene entries alone.
void write(const File& file, const std::string& projectDir, const std::string& sceneKey);

}  // namespace batap::editorConfig
