#pragma once

#include <atomic>
#include <cstdint>

namespace edgenetswitch
{
    class LifecycleIdGenerator
    {
    public:
        uint64_t next()
        {
            return counter_.fetch_add(1, std::memory_order_relaxed);
        }

    private:
        std::atomic<uint64_t> counter_{1};
    };
} // namespace edgenetswitch