#include "UIDGenerator.h"
#include <atomic>

namespace batap
{
uint64_t next_uid64()
{
    static std::atomic<uint64_t> c{1}; // 0 is null
    return c.fetch_add(1, std::memory_order_relaxed) + 1;
}
}  // namespace batap
