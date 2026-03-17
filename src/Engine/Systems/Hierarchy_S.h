#pragma once

#include <entt/entt.hpp>

#include "Components/EntityHandle.h"

namespace batap
{
struct ChildIterator
{
    entt::registry* reg{};
    entt::entity current{entt::null};

    entt::entity operator*() const { return current; }
    ChildIterator& operator++();
    bool operator==(const ChildIterator& other) const { return current == other.current; }
    bool operator!=(const ChildIterator& other) const { return current != other.current; }
};

struct ChildRange
{
    entt::registry* reg{};
    entt::entity first{entt::null};

    ChildIterator begin() { return {reg, first}; }
    ChildIterator end() { return {reg, entt::null}; }
};

struct Hierarchy_S
{
    static void attach(EntityHandle parent, EntityHandle child);
    static void detach(EntityHandle child);
    static void setParent(EntityHandle child, EntityHandle newParent);

    static bool hasParent(EntityHandle e);
    static entt::entity getParent(EntityHandle e);
    static bool isDescendantOf(EntityHandle e, EntityHandle ancestor);

    static ChildRange children(EntityHandle parent);
};
}  // namespace batap
