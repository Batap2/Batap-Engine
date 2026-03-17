#pragma once

#include <entt/entt.hpp>

namespace batap{
    struct Hierarchy_C
    {
        entt::entity parent{entt::null};
        entt::entity firstChild{entt::null};
        entt::entity nextSibling{entt::null};
        entt::entity prevSibling{entt::null};
    };
}
