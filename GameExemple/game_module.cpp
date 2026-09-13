#include "GameComponents.h"
#include "MyGame.h"

#include "GameModule.h"
#include "Serialization/AssetFieldTypes.h"
#include "Serialization/PhysicsFieldTypes.h"

extern "C" __declspec(dllexport) void batapGameEntry(batap::GameModuleAPI* out)
{
    // Fill this module's own field type slots — its component fields point at
    // them, and the host's slots are separate copies.
    batap::registerBuiltinFieldTypes();
    batap::registerAssetFieldTypes();
    batap::registerPhysicsFieldTypes();

    out->registry_ = &batap::ComponentRegistry::instance();
    out->createGame_ = []() -> batap::Game* { return new batap::MyGame(); };
    out->gameExeName_ = "GameExemple";
}
