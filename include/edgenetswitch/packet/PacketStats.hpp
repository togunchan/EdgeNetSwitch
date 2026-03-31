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
        std::uint64_t rx_packets_per_sec{0};
        std::uint64_t rx_bytes_per_sec{0};
        std::uint64_t drops_parse_error{0};
        std::uint64_t drops_validation{0};
        std::uint64_t rx_packets_per_sec_raw{0};
        std::uint64_t rx_bytes_per_sec_raw{0};
    };

    class PacketStats
    {
    public:
        explicit PacketStats(MessagingBus &bus);

        PacketMetrics snapshot() const;

        std::uint64_t rxPackets() const;
        std::uint64_t rxBytes() const;
        std::uint64_t drops() const;

        void incrementParseError();
        void incrementValidationError();
        void updateRates(std::uint64_t now_ms) const;

    private:
        std::atomic<std::uint64_t> rx_packets_{0};
        std::atomic<std::uint64_t> rx_bytes_{0};
        mutable std::uint64_t prev_rx_packets_{0};
        mutable std::uint64_t prev_rx_bytes_{0};
        mutable std::uint64_t prev_timestamp_ms_{0};
        mutable std::uint64_t last_smoothed_rx_packets_per_sec_{0};
        mutable std::uint64_t last_smoothed_rx_bytes_per_sec_{0};
        mutable std::uint64_t last_rx_packets_per_sec_raw_{0};
        mutable std::uint64_t last_rx_bytes_per_sec_raw_{0};
        mutable double smoothed_packets_per_sec_{0.0};
        mutable double smoothed_bytes_per_sec_{0.0};
        std::atomic<std::uint64_t> drops_parse_error_{0};
        std::atomic<std::uint64_t> drops_validation_{0};
    };

} // namespace edgenetswitch
