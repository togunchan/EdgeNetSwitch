#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <variant>

namespace edgenetswitch
{
    enum class MessageType : std::uint32_t
    {
        SystemStart,
        SystemShutdown,
        ConfigLoaded,
        RouteUpdated,
        TelemetryTick,
        HealthStatus,
        Telemetry,
        Health
    };

    struct TelemetryData
    {
        std::uint64_t uptime_ms;
        std::uint64_t tick_count;
        std::uint64_t timestamp_ms;
    };

    struct HealthStatus
    {
        std::uint64_t uptime_ms{};
        std::uint64_t last_heartbeat_ms{};
        std::uint64_t silence_duration_ms; // now - last_heartbeat_ms
        bool is_alive{true};
    };

    struct Message
    {
        MessageType type;
        std::uint64_t timestamp_ms;
        using Payload = std::variant<std::monostate, TelemetryData, HealthStatus>;
        Payload payload{};
    };

    class MessagingBus
    {
    public:
        using Callback = std::function<void(const Message &)>;

        MessagingBus() = default;
        ~MessagingBus() = default;

        // MessagingBus is a central, stateful system component.
        // Copying or assigning it would duplicate subscribers and break message flow.
        // Disable copy and assignment explicitly.
        MessagingBus(const MessagingBus &) = delete;
        MessagingBus &operator=(const MessagingBus &) = delete;

        void subscribe(MessageType type, Callback Callback);
        void publish(const Message &message);

    private:
        std::unordered_map<MessageType, std::vector<Callback>> subscribers_;
        std::mutex mutex_;
    };
} // namespace edgenetswitch
