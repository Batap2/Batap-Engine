#include "AssetManager.h"

#include "Handles.h"
#include "Mesh.h"

#include <cstddef>
#include <memory>
#include <utility>

namespace batap
{
AssetManager::AssetManager(ResourceManager* rm) : _resourceManager(rm) {}
AssetManager::~AssetManager() = default;
}  // namespace batap
