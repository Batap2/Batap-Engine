#pragma once

#include <cstdint>

namespace rayvox
{
struct DirtyBits
{
    uint8_t bits = 0;
    void mark(uint8_t frameIndex)
    {
        bits |= (1u << frameIndex);
    }
    void clear(uint8_t frameIndex)
    {
        bits &= ~(1u << frameIndex);
    }
    bool isDirty(uint8_t frameIndex) const
    {
        return (bits >> frameIndex) & 1u;
    }
};
}  // namespace rayvox
