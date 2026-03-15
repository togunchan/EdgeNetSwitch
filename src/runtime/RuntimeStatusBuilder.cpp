#include "RuntimeStatusBuilder.hpp"

#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/telemetry/Telemetry.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"

namespace edgenetswitch
{

    RuntimeStatus buildRuntimeStatus(
        const Telemetry &telemetry,
        const HealthMonitor &healthMonitor,
        const PacketStats &packetStats,
        RuntimeState state,
        std::uint64_t now_ms)
    {
        const PacketMetrics packet_metrics = packetStats.snapshot();

        return RuntimeStatus{
            .metrics = telemetry.snapshot(),
            .health = healthMonitor.currentStatus(),
            .packet = packet_metrics,
            .state = state,
            .snapshot_timestamp_ms = now_ms};
    }
} // namespace edgenetswitch
