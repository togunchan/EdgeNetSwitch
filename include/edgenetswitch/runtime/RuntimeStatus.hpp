#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/runtime/RuntimeMetrics.hpp"

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

    enum class OperationalHealth
    {
        Healthy,
        Degraded,
        Overloaded,
        ShuttingDown
    };

    inline std::string operationalHealthToString(OperationalHealth health)
    {
        switch (health)
        {
        case OperationalHealth::Healthy:
            return "HEALTHY";
        case OperationalHealth::Degraded:
            return "DEGRADED";
        case OperationalHealth::Overloaded:
            return "OVERLOADED";
        case OperationalHealth::ShuttingDown:
            return "SHUTTING_DOWN";
        default:
            return "UNKNOWN";
        }
    }

    struct RuntimeStatus
    {
        RuntimeMetrics metrics;
        HealthStatus health;
        RuntimeState state; // booting, running, stopping etc...
        OperationalHealth operational_health{OperationalHealth::Healthy};
        std::uint64_t snapshot_timestamp_ms{};
        std::uint64_t snapshot_version{};
        PacketMetrics packet;
    };

} // namespace edgenetswitch
