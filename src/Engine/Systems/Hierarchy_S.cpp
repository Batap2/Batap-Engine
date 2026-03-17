#include "Hierarchy_S.h"
#include "Components/Hierarchy_C.h"

namespace batap
{

static bool sameRegistry(EntityHandle a, EntityHandle b)
{
    return a._reg != nullptr && a._reg == b._reg;
}

static Hierarchy_C& getOrCreateHierarchy(EntityHandle e)
{
    if (auto* h = e.try_get<Hierarchy_C>())
        return *h;

    return e.emplace<Hierarchy_C>();
}

static void unlinkFromParentAndSiblings(EntityHandle child, Hierarchy_C& childH)
{
    auto* reg = child._reg;
    if (!reg)
        return;

    if (childH.parent != entt::null)
    {
        auto* parentH = reg->try_get<Hierarchy_C>(childH.parent);
        if (parentH && parentH->firstChild == child._entity)
            parentH->firstChild = childH.nextSibling;
    }

    if (childH.prevSibling != entt::null)
    {
        auto* prevH = reg->try_get<Hierarchy_C>(childH.prevSibling);
        if (prevH)
            prevH->nextSibling = childH.nextSibling;
    }

    if (childH.nextSibling != entt::null)
    {
        auto* nextH = reg->try_get<Hierarchy_C>(childH.nextSibling);
        if (nextH)
            nextH->prevSibling = childH.prevSibling;
    }

    childH.parent = entt::null;
    childH.prevSibling = entt::null;
    childH.nextSibling = entt::null;
}

ChildIterator& ChildIterator::operator++()
{
    auto* h = reg->try_get<Hierarchy_C>(current);
    current = h ? h->nextSibling : entt::null;
    return *this;
}

bool Hierarchy_S::hasParent(EntityHandle e)
{
    if (!e.valid())
        return false;

    if (auto* h = e.try_get<Hierarchy_C>())
        return h->parent != entt::null;

    return false;
}

entt::entity Hierarchy_S::getParent(EntityHandle e)
{
    if (!e.valid())
        return entt::null;

    if (auto* h = e.try_get<Hierarchy_C>())
        return h->parent;

    return entt::null;
}

bool Hierarchy_S::isDescendantOf(EntityHandle e, EntityHandle ancestor)
{
    if (!e.valid() || !ancestor.valid())
        return false;

    if (!sameRegistry(e, ancestor))
        return false;

    entt::entity current = getParent(e);
    auto* reg = e._reg;

    while (current != entt::null)
    {
        if (current == ancestor._entity)
            return true;

        auto* h = reg->try_get<Hierarchy_C>(current);
        if (!h)
            return false;

        current = h->parent;
    }

    return false;
}

void Hierarchy_S::detach(EntityHandle child)
{
    if (!child.valid())
        return;

    auto* childH = child.try_get<Hierarchy_C>();
    if (!childH)
        return;

    unlinkFromParentAndSiblings(child, *childH);
}

void Hierarchy_S::attach(EntityHandle parent, EntityHandle child)
{
    if (!parent.valid() || !child.valid())
        return;

    if (!sameRegistry(parent, child))
        return;

    if (parent._entity == child._entity)
        return;

    if (isDescendantOf(parent, child))
        return;

    auto& childH = getOrCreateHierarchy(child);

    if (childH.parent == parent._entity)
        return;

    unlinkFromParentAndSiblings(child, childH);

    auto& parentH = getOrCreateHierarchy(parent);

    childH.parent = parent._entity;
    childH.prevSibling = entt::null;
    childH.nextSibling = parentH.firstChild;

    if (parentH.firstChild != entt::null)
    {
        auto* firstChildH = parent._reg->try_get<Hierarchy_C>(parentH.firstChild);
        if (firstChildH)
            firstChildH->prevSibling = child._entity;
    }

    parentH.firstChild = child._entity;
}

void Hierarchy_S::setParent(EntityHandle child, EntityHandle newParent)
{
    if (!child.valid())
        return;

    if (!newParent.valid())
    {
        detach(child);
        return;
    }

    attach(newParent, child);
}

ChildRange Hierarchy_S::children(EntityHandle parent)
{
    if (!parent.valid())
        return {parent._reg, entt::null};
    auto& h = getOrCreateHierarchy(parent);
    return {parent._reg, h.firstChild};
}

}  // namespace batap
