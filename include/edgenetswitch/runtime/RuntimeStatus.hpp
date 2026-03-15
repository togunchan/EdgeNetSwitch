#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/runtime/RuntimeMetrics.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"

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
        std::uint64_t snapshot_version{};
        PacketMetrics packet;
    };

} // namespace edgenetswitch
