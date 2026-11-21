#pragma once

#include "edgenetswitch/runtime/RuntimeStatus.hpp"
#include "edgenetswitch/telemetry/WindowedEwmaRateSmoother.hpp"

#include <cstdint>

namespace edgenetswitch
{
    class Telemetry;
    class HealthMonitor;
    class PacketStats;
    class PacketProcessor;

    class RuntimeStatusBuilder
    {
    public:
        RuntimeStatusBuilder();
        explicit RuntimeStatusBuilder(const RateSmootherConfig &cfg);

        RuntimeStatus build(const Telemetry &, const HealthMonitor &, const PacketStats &,
                            const PacketProcessor &, RuntimeState, std::uint64_t now_ms);

    private:
        WindowedEwmaRateSmoother rx_packet_rate_;
        WindowedEwmaRateSmoother rx_bytes_rate_;
        std::uint64_t last_queue_overflow_count_{0};
    };

} // namespace edgenetswitch
