#pragma once

#include <entt/entt.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace batap
{
struct CascadeLight_S
{
    static std::string refusal(const entt::registry& reg, entt::entity e, std::string_view type);

    static entt::entity find(const entt::registry& reg);

    // Backstop for code that emplaces directly: entt cannot refuse a
    // construction from its signal, so the extra light is logged there and
    // removed by enforce(), at the start of the next frame.
    void connectHooks(entt::registry& reg);
    void enforce(entt::registry& reg);

   private:
    void onConstruct(entt::registry& reg, entt::entity e);

    std::vector<entt::entity> refused_;
};
}  // namespace batap
