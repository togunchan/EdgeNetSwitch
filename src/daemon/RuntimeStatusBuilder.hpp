#pragma once

#include "edgenetswitch/RuntimeStatus.hpp"

#include <cstdint>

namespace edgenetswitch
{
    class Telemetry;
    class HealthMonitor;

    RuntimeStatus buildRuntimeStatus(
        const Telemetry &,
        const HealthMonitor &,
        RuntimeState,
        std::uint64_t);
} // namespace edgenetswitch
