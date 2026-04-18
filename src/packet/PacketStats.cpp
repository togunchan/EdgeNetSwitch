#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"
#include <cmath>

namespace edgenetswitch
{
    void PacketStats::onTerminal(uint64_t id)
    {
        terminal_events_.fetch_add(1, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(seen_mutex_);
        if (!seen_.insert(id).second)
        {
            duplicate_events_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    PacketStats::PacketStats(MessagingBus &bus)
    {
        bus.subscribe(MessageType::PacketProcessed, [this](const Message &msg)
                      {
                        const Packet &p = std::get<Packet>(msg.payload);

                        rx_packets_.fetch_add(1, std::memory_order_relaxed);
                        rx_bytes_.fetch_add(p.payload_size, std::memory_order_relaxed); 
                        processed_packets_.fetch_add(1, std::memory_order_relaxed); 
                        onTerminal(p.id); });

        bus.subscribe(MessageType::PacketDropped, [this](const Message &msg)
                      {
                        const auto drop = std::get<PacketDropped>(msg.payload);
                        drop_counters_[drop.reason].fetch_add(1, std::memory_order_relaxed); 
                        onTerminal(drop.packet_id); });

        bus.subscribe(MessageType::PacketRx, [this](const Message &msg)
                      { ingress_packets_.fetch_add(1, std::memory_order_relaxed); });
    }

    PacketMetrics PacketStats::snapshotAt(std::uint64_t now_ms) const
    {
        const std::uint64_t current_packets = rx_packets_.load(std::memory_order_relaxed);
        const std::uint64_t current_bytes = rx_bytes_.load(std::memory_order_relaxed);
        const std::uint64_t ingress_packets = ingress_packets_.load(std::memory_order_relaxed);
        const std::uint64_t processed_packets = processed_packets_.load(std::memory_order_relaxed);
        const std::uint64_t processing_gap = ingress_packets >= processed_packets ? ingress_packets - processed_packets : 0;
        const std::uint64_t terminal_events = terminal_events_.load(std::memory_order_relaxed);
        const std::uint64_t pending_terminal_events = ingress_packets > terminal_events ? ingress_packets - terminal_events : 0;

        std::unordered_map<PacketDropReason, std::uint64_t> drop_snapshot;

        for (const auto &[reason, counter] : drop_counters_)
        {
            drop_snapshot[reason] = counter.load(std::memory_order_relaxed);
        }

        return PacketMetrics{
            .rx_packets = current_packets,
            .rx_bytes = current_bytes,
            .rx_packets_per_sec = 0,
            .rx_bytes_per_sec = 0,
            .drops_by_reason = std::move(drop_snapshot),
            .rx_packets_per_sec_raw = 0,
            .rx_bytes_per_sec_raw = 0,
            .ingress_packets = ingress_packets,
            .processed_packets = processed_packets,
            .processing_gap = processing_gap,
            .terminal_events = terminal_events,
            .duplicate_events = duplicate_events_.load(std::memory_order_relaxed),
            .pending_terminal_events = pending_terminal_events};
    }

    std::uint64_t PacketStats::rxPackets() const
    {
        return rx_packets_.load(std::memory_order_relaxed);
    }

    std::uint64_t PacketStats::rxBytes() const
    {
        return rx_bytes_.load(std::memory_order_relaxed);
    }

    std::uint64_t PacketStats::drops() const
    {
        std::uint64_t total = 0;
        for (const auto &[_, counter] : drop_counters_)
        {
            total += counter.load(std::memory_order_relaxed);
        }

        return total;
    }
} // namespace edgenetswitch
