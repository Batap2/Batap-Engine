#pragma once

#include <cstdint>
#include <vector>

namespace batap
{
template <typename T>
struct SlotMap
{
    struct Key
    {
        uint32_t index = 0;
        uint32_t generation = 0;

        friend bool operator==(const Key& a, const Key& b)
        {
            return a.index == b.index && a.generation == b.generation;
        }
        friend bool operator!=(const Key& a, const Key& b) { return !(a == b); }
    };

    SlotMap() = default;

    template <typename... Args>
    Key emplace(Args&&... args)
    {
        uint32_t slotIndex;
        if (!free_.empty())
        {
            slotIndex = free_.back();
            free_.pop_back();
        }
        else
        {
            slotIndex = static_cast<uint32_t>(slots_.size());
            slots_.emplace_back();
        }

        Slot& s = slots_[slotIndex];

        s.alive = true;

        uint32_t denseIndex = static_cast<uint32_t>(dense_.size());
        dense_.emplace_back(std::forward<Args>(args)...);
        denseToSlot_.push_back(slotIndex);

        s.denseIndex = denseIndex;

        return Key{slotIndex, s.generation};
    }

    bool erase(Key k)
    {
        if (!isValid(k))
            return false;

        Slot& s = slots_[k.index];
        uint32_t denseIndex = s.denseIndex;
        uint32_t lastDense = static_cast<uint32_t>(dense_.size() - 1);

        if (denseIndex != lastDense)
        {
            dense_[denseIndex] = std::move(dense_[lastDense]);
            uint32_t movedSlot = denseToSlot_[lastDense];
            denseToSlot_[denseIndex] = movedSlot;
            slots_[movedSlot].denseIndex = denseIndex;
        }

        dense_.pop_back();
        denseToSlot_.pop_back();

        s.alive = false;
        s.generation++;
        free_.push_back(k.index);

        return true;
    }

    T* get(Key k)
    {
        if (!isValid(k))
            return nullptr;
        return &dense_[slots_[k.index].denseIndex];
    }

    const T* get(Key k) const
    {
        if (!isValid(k))
            return nullptr;
        return &dense_[slots_[k.index].denseIndex];
    }

    bool contains(Key k) const { return isValid(k); }

    std::vector<T>& dense() { return dense_; }
    const std::vector<T>& dense() const { return dense_; }

    size_t size() const { return dense_.size(); }
    bool empty() const { return dense_.empty(); }

   private:
    struct Slot
    {
        uint32_t denseIndex = 0;
        uint32_t generation = 1;  // key.gen = 0 == null
        bool alive = false;
    };

    bool isValid(Key k) const
    {
        if (k.index >= slots_.size())
            return false;
        const Slot& s = slots_[k.index];
        return s.alive && s.generation == k.generation;
    }

    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    std::vector<T> dense_;
    std::vector<uint32_t> denseToSlot_;
};
}  // namespace batap
