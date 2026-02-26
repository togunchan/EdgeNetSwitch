#include "edgenetswitch/HealthMonitor.hpp"

#include <chrono>

namespace edgenetswitch
{
    namespace
    {
        std::uint64_t nowMs()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(
                    system_clock::now().time_since_epoch())
                    .count());
        }
    } // namespace

    HealthMonitor::HealthMonitor(MessagingBus &bus, std::uint64_t timeout_ms)
        : bus_(bus), timeout_ms_(timeout_ms), last_heartbeat_ms_(nowMs()), start_time_ms_(nowMs())
    {
    }

    void HealthMonitor::onHeartbeat()
    {
        last_heartbeat_ms_ = nowMs();
    }

    void HealthMonitor::onTick()
    {
        const auto now = nowMs();
        const bool current_alive = (now - last_heartbeat_ms_) <= timeout_ms_;

        if (!last_alive_state_.has_value() || current_alive != *last_alive_state_)
        {
            HealthStatus status{};
            status.uptime_ms = now - start_time_ms_;
            status.last_heartbeat_ms = last_heartbeat_ms_;
            status.is_alive = current_alive;

            Message msg{};
            msg.type = MessageType::HealthStatus;
            msg.timestamp_ms = status.last_heartbeat_ms;
            msg.payload = status;

            bus_.publish(msg);
            last_alive_state_ = current_alive;
        }
    }

    HealthSnapshot HealthMonitor::snapshot() const
    {
        const bool current_alive = (nowMs() - last_heartbeat_ms_) <= timeout_ms_;

        return HealthSnapshot{
            .alive = current_alive,
            .timeout_ms = timeout_ms_};
    }

    HealthStatus HealthMonitor::currentStatus() const
    {
        const auto now = nowMs();
        const bool current_alive = (now - last_heartbeat_ms_) <= timeout_ms_;

        HealthStatus status{};
        status.uptime_ms = now - start_time_ms_;
        status.last_heartbeat_ms = last_heartbeat_ms_;
        status.is_alive = current_alive;
        return status;
    }

} // namespace edgenetswitch
