#include "edgenetswitch/packet/PacketStats.hpp"

namespace edgenetswitch
{
    PacketStats::PacketStats(MessagingBus &bus)
    {
        bus.subscribe(MessageType::PacketRx, [this](const Message &msg)
                      {
                          const Packet &p = std::get<Packet>(msg.payload);

                          rx_packets_.fetch_add(1, std::memory_order_relaxed);
                          rx_bytes_.fetch_add(p.size_bytes, std::memory_order_relaxed);
                      });
    }

    PacketMetrics PacketStats::snapshot() const
    {
        return PacketMetrics{
            .rx_packets = rx_packets_.load(std::memory_order_relaxed),
            .rx_bytes = rx_bytes_.load(std::memory_order_relaxed),
            .drops = drops_.load(std::memory_order_relaxed)};
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
        return drops_.load(std::memory_order_relaxed);
    }
} // namespace edgenetswitch
