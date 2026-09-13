#pragma once

#include "Assets/AssetHandle.h"

namespace batap
{
struct Engine;
struct World;

struct EditorIcons
{
    void draw(World& world, Engine& ctx);
    bool show_ = true;

   private:
    TextureHandle light_;
    TextureHandle camera_;
    MaterialHandle material_;
    bool loaded_ = false;
};
}  // namespace batap
