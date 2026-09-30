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

    // Backstop for direct emplaces: entt cannot refuse from its signal, so the
    // extra light is removed by enforce() at the start of the next frame.
    void connectHooks(entt::registry& reg);
    void enforce(entt::registry& reg);

   private:
    template <class Light>
    void onConstruct(entt::registry& reg, entt::entity e);

    struct Refused
    {
        entt::entity entity_;
        void (*remove_)(entt::registry&, entt::entity);
    };
    std::vector<Refused> refused_;
};
}  // namespace batap
