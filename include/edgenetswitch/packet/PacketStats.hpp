#pragma once

#include <atomic>
#include <cstdint>

#include "edgenetswitch/messaging/MessagingBus.hpp"

namespace edgenetswitch
{
    struct PacketMetrics
    {
        std::uint64_t rx_packets{0};
        std::uint64_t rx_bytes{0};
        std::uint64_t drops{0};
    };

    class PacketStats
    {
    public:
        explicit PacketStats(MessagingBus &bus);

        PacketMetrics snapshot() const;

        std::uint64_t rxPackets() const;
        std::uint64_t rxBytes() const;
        std::uint64_t drops() const;

    private:
        std::atomic<std::uint64_t> rx_packets_{0};
        std::atomic<std::uint64_t> rx_bytes_{0};
        std::atomic<std::uint64_t> drops_{0};
    };

} // namespace edgenetswitch
