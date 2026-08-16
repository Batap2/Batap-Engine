#include "Transform_S.h"

#include "Components/ComponentFlag.h"
#include "Components/EntityHandle.h"
#include "Components/Hierarchy_C.h"
#include "Systems/Hierarchy_S.h"
#include "Instance/InstanceManager.h"

#include "emhash/hash_set8.hpp"

#include <algorithm>

namespace batap
{

void Transform_S::ensure_chain_up_to_date(EntityHandle h, GPUInstanceManager& instanceManager)
{
    auto& reg = *h.reg_;

    std::vector<entt::entity> path;
    path.reserve(32);

    entt::entity cur = h.entity_;
    while (cur != entt::null && reg.valid(cur) && reg.any_of<Transform_C>(cur))
    {
        path.push_back(cur);
        const entt::entity p = Hierarchy_S::getParent({&reg, cur});
        if (p == entt::null)
            break;
        cur = p;
    }

    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        const entt::entity node = *it;
        auto& t = reg.get<Transform_C>(node);

        if (t.localDirty_)
        {
            t.local_ = TRS_Transform(t.localPosition_, t.localRotation_, t.localScale_);
            t.localDirty_ = false;
        }

        const entt::entity p = Hierarchy_S::getParent({&reg, node});
        if (p != entt::null && reg.valid(p) && reg.any_of<Transform_C>(p))
        {
            t.world_ = reg.get<Transform_C>(p).world_ * t.local_;
        }
        else
        {
            t.world_ = t.local_;
        }
        instanceManager.markDirty({&reg, node}, ComponentFlag::Transform);
    }
}

void Transform_S::markDirty(EntityHandle h)
{
    if (!has_transform(h))
        return;
    auto& reg = *h.reg_;
    auto& t = reg.get<Transform_C>(h.entity_);

    if (t.dirtyStamp_ == frameCount)
        return;

    t.dirtyStamp_ = frameCount;
    dirty.push_back(h.entity_);
}

void Transform_S::setLocalPosition(EntityHandle h, const v3f& p)
{
    if (!has_transform(h))
        return;
    auto& t = h.get<Transform_C>();
    t.localPosition_ = p;
    t.localDirty_ = true;
    markDirty(h);
}

void Transform_S::setLocalRotation(EntityHandle h, const quatf& q)
{
    if (!has_transform(h))
        return;
    auto& t = h.get<Transform_C>();
    t.localRotation_ = q.normalized();
    t.localDirty_ = true;
    markDirty(h);
}

void Transform_S::setLocalScale(EntityHandle h, const v3f& s)
{
    if (!has_transform(h))
        return;
    auto& t = h.get<Transform_C>();
    t.localScale_ = s;
    t.localDirty_ = true;
    markDirty(h);
}

void Transform_S::translate(EntityHandle h, const v3f& vec, Space space)
{
    if (!has_transform(h))
        return;
    auto& reg = *h.reg_;
    auto& t = h.get<Transform_C>();

    switch (space)
    {
        case Space::Local:
            t.localPosition_ += t.localRotation_ * vec;
            break;

        case Space::Parent:
            t.localPosition_ += vec;
            break;

        case Space::World: {
            const entt::entity p = Hierarchy_S::getParent(h);
            if (p != entt::null && reg.valid(p) && reg.any_of<Transform_C>(p))
            {
                const transform& pw = reg.get<Transform_C>(p).world_;
                const transform inv = pw.inverse();
                t.localPosition_ += (inv * vec);
            }
            else
            {
                t.localPosition_ += vec;
            }
            break;
        }
    }

    t.localDirty_ = true;
    markDirty(h);
}

void Transform_S::rotate(EntityHandle h, const quatf& delta, Space space)
{
    if (!has_transform(h))
        return;
    auto& reg = *h.reg_;
    auto& t = reg.get<Transform_C>(h.entity_);
    const quatf d = delta.normalized();

    switch (space)
    {
        case Space::Local:
            t.localRotation_ = (t.localRotation_ * d).normalized();
            break;

        case Space::Parent:
            t.localRotation_ = (d * t.localRotation_).normalized();
            break;

        case Space::World: {
            const entt::entity p = Hierarchy_S::getParent(h);
            if (p != entt::null && reg.valid(p) && reg.any_of<Transform_C>(p))
            {
                const quatf Qp = Transform_C::extractWorldRotation(reg.get<Transform_C>(p).world_);
                t.localRotation_ = (Qp.conjugate() * d * Qp * t.localRotation_).normalized();
            }
            else
            {
                t.localRotation_ = (d * t.localRotation_).normalized();
            }
            break;
        }
    }

    t.localDirty_ = true;
    markDirty(h);
}

void Transform_S::rotate(EntityHandle h, const v3f& axis, float radians, Space space)
{
    if (axis.squaredNorm() == 0.f)
        return;
    rotate(h, quatf(angleaxisf(radians, axis.normalized())), space);
}

void Transform_S::scale(EntityHandle h, const v3f& vec)
{
    if (!has_transform(h))
        return;
    auto& reg = *h.reg_;
    auto& t = reg.get<Transform_C>(h.entity_);
    t.localScale_ = t.localScale_.cwiseProduct(vec);
    t.localDirty_ = true;
    markDirty(h);
}

void Transform_S::flushDirty(entt::registry& reg, GPUInstanceManager& instanceManager)
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
            const entt::entity p = Hierarchy_S::getParent({&reg, cur});
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
            const entt::entity p = Hierarchy_S::getParent({&reg, root});
            if (has_transform_e(p))
            {
                ensure_chain_up_to_date(EntityHandle{&reg, p}, instanceManager);
                pw = reg.get<Transform_C>(p).world_;
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

            const bool dirtyHere = it.parentDirty || t.localDirty_;

            if (t.localDirty_)
            {
                t.local_ = TRS_Transform(t.localPosition_, t.localRotation_, t.localScale_);
                t.localDirty_ = false;
            }

            if (dirtyHere)
            {
                t.world_ = it.parentWorld * t.local_;
                instanceManager.markDirty({&reg, it.e}, ComponentFlag::Transform);
            }

            const transform childPW = t.world_;
            const bool childParentDirty = dirtyHere;


            for (const auto ch : Hierarchy_S::children({&reg, it.e}))
            {
                if (!has_transform_e(ch))
                    continue;
                stack.push_back(Item{ch, childPW, childParentDirty});
            }
        }
    }
}

void Transform_S::update(entt::registry& reg, GPUInstanceManager& instanceManager)
{
    ++frameCount;
    flushDirty(reg, instanceManager);
}

}  // namespace batap
