#pragma once

#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "edgenetswitch/messaging/MessagingBus.hpp"

namespace edgenetswitch
{
    struct PacketMetrics
    {
        std::uint64_t rx_packets{0};
        std::uint64_t rx_bytes{0};
        std::uint64_t rx_packets_per_sec{0};
        std::uint64_t rx_bytes_per_sec{0};
        std::unordered_map<PacketDropReason, std::uint64_t> drops_by_reason;
        std::uint64_t rx_packets_per_sec_raw{0};
        std::uint64_t rx_bytes_per_sec_raw{0};
        std::uint64_t ingress_packets{0};
        std::uint64_t processed_packets{0};
        std::uint64_t processing_gap{0};
        std::uint64_t terminal_events{0};
        std::uint64_t duplicate_events{0};
        std::uint64_t pending_terminal_events{0};
    };

    class PacketStats
    {
    public:
        explicit PacketStats(MessagingBus &bus);

        PacketMetrics snapshotAt(std::uint64_t now_ms) const;

        std::uint64_t rxPackets() const;
        std::uint64_t rxBytes() const;
        std::uint64_t drops() const;

        void incrementParseError();
        void incrementValidationError();
        void updateRates(std::uint64_t now_ms) const;
        void onTerminal(uint64_t lifecycle_id);

    private:
        std::atomic<std::uint64_t> rx_packets_{0};
        std::atomic<std::uint64_t> rx_bytes_{0};
        std::unordered_map<PacketDropReason, std::atomic<uint64_t>> drop_counters_;
        std::atomic<std::uint64_t> ingress_packets_{0};
        std::atomic<std::uint64_t> processed_packets_{0};
        std::atomic<std::uint64_t> terminal_events_{0};
        std::atomic<std::uint64_t> duplicate_events_{0};
        std::unordered_set<uint64_t> completed_lifecycles_;
        std::mutex lifecycle_mutex_;
    };

} // namespace edgenetswitch
