#pragma once

#include <memory>
#include "IApp.h"
#include "World.h"

namespace batap{
    struct Context;

    struct App : IApp{
        std::unique_ptr<World> world_;
        
        void start(Context& ctx) override;
        void update(Context& ctx) override;
        void shutdown(Context& ctx) override;
    };
}
