#include "Systems/Planetshine_S.h"

#include "Components/DirectionalLight_C.h"
#include "Components/EditorOnly_C.h"
#include "Components/FarPointLight_C.h"
#include "Components/Planetshine_C.h"
#include "Components/PointLight_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Instance/InstanceManager.h"
#include "Systems/CascadeLight_S.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <vector>

namespace batap
{
namespace
{
struct PlanetshineLight_C
{
    entt::entity body_ = entt::null;
};

struct Star
{
    bool valid_ = false;
    bool directional_ = false;
    v3f toStar_ = v3f::Zero();
    v3f pos_ = v3f::Zero();
    col3 color_ = {1, 1, 1};
    float illuminance_ = 0.f;
};

Star findStar(entt::registry& reg)
{
    Star star;
    const entt::entity e = CascadeLight_S::find(reg);
    const auto* tc = e == entt::null ? nullptr : reg.try_get<Transform_C>(e);
    if (!tc)
        return star;
    if (const auto* sun = reg.try_get<DirectionalLight_C>(e))
    {
        star.valid_ = true;
        star.directional_ = true;
        star.toStar_ = tc->world().linear().col(2).normalized();
        star.color_ = sun->color_;
        star.illuminance_ = sun->intensity_;
    }
    else if (const auto* far = reg.try_get<FarPointLight_C>(e))
    {
        star.valid_ = true;
        star.pos_ = tc->world().translation();
        star.color_ = far->color_;
        star.illuminance_ = far->intensity_;
    }
    return star;
}

float lambertSpherePhase(float cosAlpha)
{
    constexpr float pi = std::numbers::pi_v<float>;
    const float a = std::acos(std::clamp(cosAlpha, -1.f, 1.f));
    return (std::sin(a) + (pi - a) * std::cos(a)) / pi;
}
}  // namespace

void Planetshine_S::update(World& world, Engine& ctx)
{
    entt::registry& reg = world.registry_;
    std::unordered_map<entt::entity, entt::entity> lights;
    std::vector<entt::entity> stale;
    for (auto [light, tag] : reg.view<PlanetshineLight_C>().each())
    {
        if (reg.valid(tag.body_) && reg.all_of<Planetshine_C, Transform_C>(tag.body_) &&
            !lights.contains(tag.body_))
            lights.emplace(tag.body_, light);
        else
            stale.push_back(light);
    }
    for (entt::entity light : stale)
        world.destroy({&reg, light});

    const Star star = findStar(reg);
    const entt::entity camera = world.renderCamera();
    const Transform_C* eye = reg.valid(camera) ? reg.try_get<Transform_C>(camera) : nullptr;
    const bool shadows =
        ctx.rtSupported() && ctx.settings().rayTracing_ && ctx.settings().rtShadows_;

    for (auto [body, shine, transform] : reg.view<Planetshine_C, Transform_C>().each())
    {
        entt::entity light = entt::null;
        if (const auto it = lights.find(body); it != lights.end())
            light = it->second;
        else
        {
            const EntityHandle spawned = world.spawn("pointLight");
            if (!spawned.valid())
                continue;
            reg.emplace<EditorOnly_C>(spawned.entity_);
            reg.emplace<PlanetshineLight_C>(spawned.entity_, body);
            light = spawned.entity_;
        }

        const v3f centre = transform.world().translation();
        const float radius = shine.radius_ * transform.world().linear().colwise().norm().maxCoeff();
        const float albedo = shine.albedo_.maxCoeff();
        float intensity = 0.f;
        float distance = radius;
        if (star.valid_ && eye && albedo > 0.f)
        {
            const v3f toEye = eye->world().translation() - centre;
            distance = std::max(toEye.norm(), radius);
            const v3f toStar = star.directional_ ? star.toStar_
                                                 : v3f{(star.pos_ - centre).normalized()};
            intensity = star.illuminance_ * (2.f / 3.f) * albedo * radius * radius *
                        lambertSpherePhase(toStar.dot(toEye / distance));
        }

        EntityHandle handle{&reg, light};
        handle.setLocalPosition(centre);
        PointLight_C& point = reg.get<PointLight_C>(light);
        point.color_ = col3{star.color_.cwiseProduct(shine.albedo_) / std::max(albedo, 1e-6f)};
        point.intensity_ = intensity;
        point.radius_ = 4.f * distance + radius;
        point.falloff_ = 0.f;
        point.sourceRadius_ = radius;
        point.castShadows_ = shadows;
        world.instances().markDirty<PointLight_C>(handle);
    }
}
}  // namespace batap
