#pragma once

#include "edgenetswitch/RuntimeStatus.hpp"
#include "edgenetswitch/Telemetry.hpp"

namespace edgenetswitch
{

    inline RuntimeStatus buildRuntimeStatus(
        const Telemetry &telemetry,
        RuntimeState state)
    {
        return RuntimeStatus{
            .metrics = telemetry.snapshot(),
            .state = state};
    }
} // namespace edgenetswitch