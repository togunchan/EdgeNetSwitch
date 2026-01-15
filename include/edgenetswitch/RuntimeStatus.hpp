#pragma once

#include "edgenetswitch/RuntimeMetrics.hpp"
#include <string>

namespace edgenetswitch
{

    enum class RuntimeState
    {
        Booting,
        Running,
        Stopping
    };

    inline std::string stateToString(RuntimeState s)
    {
        switch (s)
        {
        case RuntimeState::Booting:
            return "BOOTING";
        case RuntimeState::Running:
            return "RUNNING";
        case RuntimeState::Stopping:
            return "STOPPING";

        default:
            return "UNKNOWN";
        }
    }

    struct RuntimeStatus
    {
        RuntimeMetrics metrics;
        RuntimeState state; // booting, running, stopping etc...
    };

} // namespace edgenetswitch