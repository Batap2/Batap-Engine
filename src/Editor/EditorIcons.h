#pragma once

#include "Assets/AssetHandle.h"
#include "Handles.h"

#include <cstdint>

namespace batap
{
struct Engine;
struct World;
struct ResourceManager;

struct EditorIcons
{
    ~EditorIcons();

    void draw(World& world, Engine& ctx);
    bool show_ = true;

   private:
    struct Icon
    {
        GPUResourceHandle texture_;
        uint32_t bindlessIndex_ = 0xFFFFFFFFu;
    };

    void load(Engine& ctx);

    Icon light_;
    Icon camera_;
    MaterialHandle material_;
    ResourceManager* resources_ = nullptr;
    bool loaded_ = false;
};
}  // namespace batap
