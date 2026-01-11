#pragma once

#include "edgenetswitch/Telemetry.hpp"
#include "edgenetswitch/RuntimeStatus.hpp"
#include "edgenetswitch/HealthMonitor.hpp"

namespace edgenetswitch::control
{

    struct ControlContext
    {
        const Telemetry &telemetry;
        const RuntimeState &runtimeState;
        const HealthMonitor &healthMotitor;
    };

} // namespace edgenetswitch::control
