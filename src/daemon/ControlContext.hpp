#pragma once

#include "edgenetswitch/Telemetry.hpp"
#include "edgenetswitch/RuntimeStatus.hpp"

namespace edgenetswitch::control
{

    struct ControlContext
    {
        const Telemetry &telemetry;
        const RuntimeState &runtimeState;
    };

} // namespace edgenetswitch::control