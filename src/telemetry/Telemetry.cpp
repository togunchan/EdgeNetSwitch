#include "edgenetswitch/Telemetry.hpp"

#include <chrono>
#include <variant>
#include <cstdint>

namespace edgenetswitch
{
    namespace
    {
        std::uint64_t nowMs()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }
    }

    Telemetry::Telemetry(MessagingBus &bus, const Config &) : bus_(bus), start_time_ms_(nowMs()), tick_count_(0) {}

    void Telemetry::onTick()
    {
        ++tick_count_;

        TelemetryData data{};
        data.uptime_ms = nowMs() - start_time_ms_;
        data.tick_count = tick_count_;
        data.timestamp_ms = nowMs();

        Message msg{};
        msg.type = MessageType::Telemetry;
        msg.timestamp_ms = data.timestamp_ms;
        msg.payload = data;

        bus_.publish(msg);
    }

    RuntimeMetrics Telemetry::snapshot() const
    {
        return RuntimeMetrics{
            .uptime_ms = nowMs() - start_time_ms_,
            .tick_count = tick_count_};
    }
} // namespace edgenetswitch