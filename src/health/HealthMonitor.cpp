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
    }

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
        HealthStatus status{};
        status.uptime_ms = nowMs() - start_time_ms_;
        status.last_heartbeat_ms = last_heartbeat_ms_;
        status.is_alive = (nowMs() - last_heartbeat_ms_) <= timeout_ms_;

        Message msg{};
        msg.type = MessageType::Health;
        msg.timestamp_ms = status.last_heartbeat_ms;
        msg.payload = status;

        bus_.publish(msg);
    }

} // namespace edgenetswitch