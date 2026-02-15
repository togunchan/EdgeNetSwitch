#pragma once

#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/RuntimeMetrics.hpp"
#include <cstdint>
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
        HealthStatus health;
        RuntimeState state; // booting, running, stopping etc...
        std::uint64_t snapshot_timestamp_ms{};
    };

} // namespace edgenetswitch
