#pragma once

#include "edgenetswitch/runtime/RuntimeStatus.hpp"

#include <cstdint>

namespace edgenetswitch
{
    class Telemetry;
    class HealthMonitor;
    class PacketStats;

    RuntimeStatus buildRuntimeStatus(
        const Telemetry &,
        const HealthMonitor &,
        const PacketStats &,
        RuntimeState,
        std::uint64_t);
} // namespace edgenetswitch
