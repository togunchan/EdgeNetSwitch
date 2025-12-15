#pragma once

#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/Config.hpp"

#include <cstdint>

namespace edgenetswitch
{
    class HealthMonitor
    {
    public:
        HealthMonitor(MessagingBus &bus, std::uint64_t timeout_ms);

        void onTick();
        void onHeartbeat();

    private:
        MessagingBus &bus_;
        std::uint64_t timeout_ms_;
        std::uint64_t last_heartbeat_ms_;
        std::uint64_t start_time_ms_;
    };
} // namespace edgenetswitch