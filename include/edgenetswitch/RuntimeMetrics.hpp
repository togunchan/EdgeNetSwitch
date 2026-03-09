#pragma once

#include <cstdint>

namespace edgenetswitch
{
    struct RuntimeMetrics
    {
        std::uint64_t uptime_ms{0};
        std::uint64_t tick_count{0};
        std::size_t telemetry_queue_size{0};
        uint64_t telemetry_dropped_samples{0};
    };
}
