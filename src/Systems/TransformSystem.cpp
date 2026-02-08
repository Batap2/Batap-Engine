#include "TransformSystem.h"

#include "Components/ComponentFlag.h"
#include "Instance/InstanceManager.h"

#include "emhash/hash_set8.hpp"

#include <algorithm>

namespace rayvox
{

// --------- ensure chain (root->...->e) ---------
void TransformSystem::ensure_chain_up_to_date(EntityHandle h, Context& ctx)
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
        ctx._gpuInstanceManager->markDirty({&reg, node}, ComponentFlag::Transform);
    }
}

// --------- Dirty queue (STAMP O(1) DEDUP) ---------
void TransformSystem::markDirty(EntityHandle h)
{
    if (!has_transform(h))
        return;
    auto& reg = *h._reg;
    auto& t = reg.get<Transform_C>(h._entity);

    // déjà enqueued pour cette frame
    if (t._dirtyStamp == frameCount)
        return;

    t._dirtyStamp = frameCount;
    dirty.push_back(h._entity);
}

// --------- API Mutations ---------
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
                //ensure_chain_up_to_date(EntityHandle{&reg, p});
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
                //ensure_chain_up_to_date(EntityHandle{&reg, p});
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

// --------- setParent (keepWorld) ---------
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

    // anti-cycle
    for (entt::entity cur = newParent; cur != entt::null;)
    {
        if (cur == child)
            return;
        auto& t = reg.get<Transform_C>(cur);
        cur = to_entity(t._parent);
    }

    // matrices à jour
    // ensure_chain_up_to_date(childH);
    // if (newParent != entt::null)
    //     ensure_chain_up_to_date(EntityHandle{&reg, newParent});

    const transform oldWorld = ct._world;

    // detach oldParent
    if (oldParent != entt::null)
    {
        auto& pt = reg.get<Transform_C>(oldParent);
        auto& v = pt.children;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const EntityHandle& h) { return to_entity(h) == child; }),
                v.end());
    }

    // assign parent
    ct._parent = (newParent == entt::null) ? EntityHandle{} : EntityHandle{&reg, newParent};

    // attach newParent
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

void TransformSystem::flushDirty(entt::registry& reg, Context& ctx)
{
    (void) ctx;
    if (dirty.empty())
        return;

    // valid filter
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

    // 3) Trouver les roots = ancêtre le plus haut qui est encore dirty
    emhash8::HashSet<entt::entity> rootsSet;
    rootsSet.reserve(dirtySet.size());

    for (auto e : dirtySet)
    {
        entt::entity cur = e;

        // Remonte tant que le parent est dirty aussi
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

    // 4) DFS depuis chaque root
    struct Item
    {
        entt::entity e;
        transform parentWorld;
        bool parentDirty;
    };

    std::vector<Item> stack;
    stack.reserve(256);  // grossira si gros subtree

    for (auto root : rootsSet)
    {
        // parentWorld initial = world du parent (s'il existe)
        transform pw = transform::Identity();
        {
            auto& rt = reg.get<Transform_C>(root);
            const entt::entity p = rt._parent._entity;
            if (has_transform_e(p))
            {
                ensure_chain_up_to_date(EntityHandle{&reg, p}, ctx);
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

            // Si le parent a bougé => world doit être recalculé même si local pas dirty
            const bool dirtyHere = it.parentDirty || t._localDirty;

            if (t._localDirty)
            {
                t._local = TRS_Transform(t._localPosition, t._localRotation, t._localScale);
                t._localDirty = false;
            }

            if (dirtyHere)
            {
                t._world = it.parentWorld * t._local;
                ctx._gpuInstanceManager->markDirty({&reg, it.e}, ComponentFlag::Transform);
            }

            const transform childPW = t._world;
            const bool childParentDirty = dirtyHere;

            // Push enfants
            // (micro-opt: éviter reg.valid/any_of dans la boucle si tu garantis que children
            // ne contient que des entités valides + Transform_C. Sinon on garde les checks)
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

void TransformSystem::update(entt::registry& reg, Context& ctx)
{
    ++frameCount;
    flushDirty(reg, ctx);
}

}  // namespace rayvox
