#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>

namespace edgenetswitch
{
    enum class MessageType : std::uint32_t
    {
        SystemStart,
        SystemShutdown,
        ConfigLoaded,
        RouteUpdated,
        TelemetryTick,
        HealthStatus
    };

    struct Message
    {
        MessageType type;
        std::uint64_t timestamp;
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