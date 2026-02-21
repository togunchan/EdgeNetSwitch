#include "RuntimeStatusBuilder.hpp"

#include "edgenetswitch/HealthMonitor.hpp"
#include "edgenetswitch/Telemetry.hpp"

namespace edgenetswitch
{

    RuntimeStatus buildRuntimeStatus(
        const Telemetry &telemetry,
        const HealthMonitor &healthMonitor,
        RuntimeState state,
        std::uint64_t now_ms)
    {
        return RuntimeStatus{
            .metrics = telemetry.snapshot(),
            .health = healthMonitor.currentStatus(),
            .state = state,
            .snapshot_timestamp_ms = now_ms};
    }
} // namespace edgenetswitch
