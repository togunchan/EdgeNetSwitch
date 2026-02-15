#pragma once

#include <cstdint>

namespace edgenetswitch
{
    struct RuntimeMetrics
    {
        std::uint64_t uptime_ms;
        std::uint64_t tick_count;
    };
}
