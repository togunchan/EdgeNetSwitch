#pragma once

#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/Config.hpp"

#include <cstdint>
#include <optional>

namespace edgenetswitch
{
    struct HealthSnapshot
    {
        bool alive;
        uint64_t timeout_ms;
    };

    class HealthMonitor
    {
    public:
        HealthMonitor(MessagingBus &bus, std::uint64_t timeout_ms);

        void onTick();
        void onHeartbeat();
        HealthSnapshot snapshot() const;
        HealthStatus currentStatus() const;

    private:
        MessagingBus &bus_;
        std::uint64_t timeout_ms_;
        std::uint64_t last_heartbeat_ms_;
        std::uint64_t start_time_ms_;
        std::optional<bool> last_alive_state_;
    };
} // namespace edgenetswitch
