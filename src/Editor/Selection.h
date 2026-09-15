#pragma once

#include "Components/EntityHandle.h"

#include <algorithm>
#include <vector>

namespace batap
{
struct Selection
{
    bool empty() const { return entities_.empty(); }
    size_t size() const { return entities_.size(); }
    const std::vector<EntityHandle>& all() const { return entities_; }

    bool contains(EntityHandle e) const
    {
        return std::find(entities_.begin(), entities_.end(), e) != entities_.end();
    }

    EntityHandle primary() const { return entities_.empty() ? EntityHandle{} : entities_.back(); }

    void clear() { entities_.clear(); }

    void set(EntityHandle e)
    {
        entities_.clear();
        add(e);
    }

    void add(EntityHandle e)
    {
        if (e.valid() && !contains(e))
            entities_.push_back(e);
    }

    void toggle(EntityHandle e)
    {
        const auto it = std::find(entities_.begin(), entities_.end(), e);
        if (it != entities_.end())
            entities_.erase(it);
        else
            add(e);
    }

    void prune()
    {
        std::erase_if(entities_, [](const EntityHandle& e) { return !e.valid(); });
    }

   private:
    std::vector<EntityHandle> entities_;
};
}  // namespace batap
