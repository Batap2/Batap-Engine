#include "TransformSystem.h"

#include "Components/ComponentFlag.h"
#include "Instance/InstanceManager.h"

#include "emhash/hash_set8.hpp"

#include <algorithm>

namespace batap
{

void TransformSystem::ensure_chain_up_to_date(EntityHandle h, GPUInstanceManager& instanceManager)
{
    auto& reg = *h._reg;

    std::vector<entt::entity> path;
    path.reserve(32);

    entt::entity cur = h._entity;
    while (cur != entt::null && reg.valid(cur) && reg.any_of<Transform_C>(cur))
    {
        path.push_back(cur);
        auto& t = reg.get<Transform_C>(cur);
        const entt::entity p = to_entity(t._parent);
        if (p == entt::null)
            break;
        cur = p;
    }

    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        const entt::entity node = *it;
        auto& t = reg.get<Transform_C>(node);

        if (t._localDirty)
        {
            t._local = TRS_Transform(t._localPosition, t._localRotation, t._localScale);
            t._localDirty = false;
        }

        const entt::entity p = to_entity(t._parent);
        if (p != entt::null && reg.valid(p) && reg.any_of<Transform_C>(p))
        {
            t._world = reg.get<Transform_C>(p)._world * t._local;
        }
        else
        {
            t._world = t._local;
        }
        instanceManager.markDirty({&reg, node}, ComponentFlag::Transform);
    }
}

void TransformSystem::markDirty(EntityHandle h)
{
    if (!has_transform(h))
        return;
    auto& reg = *h._reg;
    auto& t = reg.get<Transform_C>(h._entity);

    if (t._dirtyStamp == frameCount)
        return;

    t._dirtyStamp = frameCount;
    dirty.push_back(h._entity);
}

void TransformSystem::setLocalPosition(EntityHandle h, const v3f& p)
{
    if (!has_transform(h))
        return;
    auto& t = h.get<Transform_C>();
    t._localPosition = p;
    t._localDirty = true;
    markDirty(h);
}

void TransformSystem::setLocalRotation(EntityHandle h, const quatf& q)
{
    if (!has_transform(h))
        return;
    auto& t = h.get<Transform_C>();
    t._localRotation = q.normalized();
    t._localDirty = true;
    markDirty(h);
}

void TransformSystem::setLocalScale(EntityHandle h, const v3f& s)
{
    if (!has_transform(h))
        return;
    auto& t = h.get<Transform_C>();
    t._localScale = s;
    t._localDirty = true;
    markDirty(h);
}

void TransformSystem::translate(EntityHandle h, const v3f& vec, Space space)
{
    if (!has_transform(h))
        return;
    auto& reg = *h._reg;
    auto& t = h.get<Transform_C>();

    switch (space)
    {
        case Space::Local:
            t._localPosition += t._localRotation * vec;
            break;

        case Space::Parent:
            t._localPosition += vec;
            break;

        case Space::World: {
            const entt::entity p = to_entity(t._parent);
            if (p != entt::null && reg.valid(p) && reg.any_of<Transform_C>(p))
            {
                const transform& pw = reg.get<Transform_C>(p)._world;
                const transform inv = pw.inverse();
                t._localPosition += (inv * vec);
            }
            else
            {
                t._localPosition += vec;
            }
            break;
        }
    }

    t._localDirty = true;
    markDirty(h);
}

void TransformSystem::rotate(EntityHandle h, const quatf& delta, Space space)
{
    if (!has_transform(h))
        return;
    auto& reg = *h._reg;
    auto& t = reg.get<Transform_C>(h._entity);
    const quatf d = delta.normalized();

    switch (space)
    {
        case Space::Local:
            t._localRotation = (t._localRotation * d).normalized();
            break;

        case Space::Parent:
            t._localRotation = (d * t._localRotation).normalized();
            break;

        case Space::World: {
            const entt::entity p = to_entity(t._parent);
            if (p != entt::null && reg.valid(p) && reg.any_of<Transform_C>(p))
            {
                const quatf Qp = Transform_C::extractWorldRotation(reg.get<Transform_C>(p)._world);
                t._localRotation = (Qp.conjugate() * d * Qp * t._localRotation).normalized();
            }
            else
            {
                t._localRotation = (d * t._localRotation).normalized();
            }
            break;
        }
    }

    t._localDirty = true;
    markDirty(h);
}

void TransformSystem::rotate(EntityHandle h, const v3f& axis, float radians, Space space)
{
    if (axis.squaredNorm() == 0.f)
        return;
    rotate(h, quatf(angleaxisf(radians, axis.normalized())), space);
}

void TransformSystem::scale(EntityHandle h, const v3f& vec)
{
    if (!has_transform(h))
        return;
    auto& reg = *h._reg;
    auto& t = reg.get<Transform_C>(h._entity);
    t._localScale = t._localScale.cwiseProduct(vec);
    t._localDirty = true;
    markDirty(h);
}

void TransformSystem::setParent(EntityHandle childH, entt::entity newParent, bool keepWorld)
{
    if (!has_transform(childH))
        return;
    auto& reg = *childH._reg;
    const entt::entity child = childH._entity;

    auto& ct = reg.get<Transform_C>(child);
    const entt::entity oldParent = to_entity(ct._parent);

    if (newParent == child)
        return;
    if (newParent == oldParent)
        return;
    if (newParent != entt::null && !(reg.valid(newParent) && reg.any_of<Transform_C>(newParent)))
        return;

    for (entt::entity cur = newParent; cur != entt::null;)
    {
        if (cur == child)
            return;
        auto& t = reg.get<Transform_C>(cur);
        cur = to_entity(t._parent);
    }

    const transform oldWorld = ct._world;

    if (oldParent != entt::null)
    {
        auto& pt = reg.get<Transform_C>(oldParent);
        auto& v = pt.children;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const EntityHandle& h) { return to_entity(h) == child; }),
                v.end());
    }

    ct._parent = (newParent == entt::null) ? EntityHandle{} : EntityHandle{&reg, newParent};

    if (newParent != entt::null)
    {
        auto& np = reg.get<Transform_C>(newParent);
        auto& v = np.children;
        const bool exists = std::any_of(v.begin(), v.end(), [&](const EntityHandle& h)
                                        { return to_entity(h) == child; });
        if (!exists)
            v.push_back(EntityHandle{&reg, child});
    }

    if (keepWorld)
    {
        const transform parentWorld = (newParent != entt::null)
                                          ? reg.get<Transform_C>(newParent)._world
                                          : transform::Identity();

        const transform localT = parentWorld.inverse() * oldWorld;
        ct.setLocalFromTransform(localT);
        ct._localDirty = true;
    }

    ct._localDirty = true;
    markDirty(childH);
}

void TransformSystem::flushDirty(entt::registry& reg, GPUInstanceManager& instanceManager)
{
    if (dirty.empty())
        return;

    emhash8::HashSet<entt::entity> dirtySet;
    dirtySet.reserve(static_cast<unsigned int>(dirty.size()) * 2);
    for (auto e : dirty)
    {
        if (e == entt::null)
            continue;
        if (!reg.valid(e) || !reg.any_of<Transform_C>(e))
            continue;
        dirtySet.insert(e);
    }
    dirty.clear();
    if (dirtySet.empty())
        return;

    auto has_transform_e = [&](entt::entity e) -> bool
    { return e != entt::null && reg.valid(e) && reg.any_of<Transform_C>(e); };

    emhash8::HashSet<entt::entity> rootsSet;
    rootsSet.reserve(dirtySet.size());

    for (auto e : dirtySet)
    {
        entt::entity cur = e;

        while (true)
        {
            auto& tc = reg.get<Transform_C>(cur);
            const entt::entity p = to_entity(tc._parent);
            if (!has_transform_e(p))
                break;
            if (!dirtySet.contains(p))
                break;
            cur = p;
        }

        rootsSet.insert(cur);
    }

    struct Item
    {
        entt::entity e;
        transform parentWorld;
        bool parentDirty;
    };

    std::vector<Item> stack;
    stack.reserve(256);

    for (auto root : rootsSet)
    {
        transform pw = transform::Identity();
        {
            auto& rt = reg.get<Transform_C>(root);
            const entt::entity p = rt._parent._entity;
            if (has_transform_e(p))
            {
                ensure_chain_up_to_date(EntityHandle{&reg, p}, instanceManager);
                pw = reg.get<Transform_C>(p)._world;
            }
        }

        stack.clear();
        stack.push_back(Item{root, pw, false});

        while (!stack.empty())
        {
            Item it = stack.back();
            stack.pop_back();

            if (!has_transform_e(it.e))
                continue;

            auto& t = reg.get<Transform_C>(it.e);

            const bool dirtyHere = it.parentDirty || t._localDirty;

            if (t._localDirty)
            {
                t._local = TRS_Transform(t._localPosition, t._localRotation, t._localScale);
                t._localDirty = false;
            }

            if (dirtyHere)
            {
                t._world = it.parentWorld * t._local;
                instanceManager.markDirty({&reg, it.e}, ComponentFlag::Transform);
            }

            const transform childPW = t._world;
            const bool childParentDirty = dirtyHere;

            for (const EntityHandle& ch : t.children)
            {
                const entt::entity ce = to_entity(ch);
                if (!has_transform_e(ce))
                    continue;
                stack.push_back(Item{ce, childPW, childParentDirty});
            }
        }
    }
}

void TransformSystem::update(entt::registry& reg, GPUInstanceManager& instanceManager)
{
    ++frameCount;
    flushDirty(reg, instanceManager);
}

}  // namespace batap
