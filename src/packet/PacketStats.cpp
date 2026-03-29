#include "edgenetswitch/packet/PacketStats.hpp"

namespace edgenetswitch
{
    PacketStats::PacketStats(MessagingBus &bus)
    {
        bus.subscribe(MessageType::PacketProcessed, [this](const Message &msg)
                      {
                          const Packet &p = std::get<Packet>(msg.payload);

                          rx_packets_.fetch_add(1, std::memory_order_relaxed);
                          rx_bytes_.fetch_add(p.payload_size, std::memory_order_relaxed); });

        bus.subscribe(MessageType::PacketDropped, [this](const Message &msg)
                      {
            auto reason = std::get<PacketDropReason>(msg.payload);

            switch (reason)
            {
            case PacketDropReason::ParseError:
                incrementParseError();
                break;

            case PacketDropReason::ValidationError:
                incrementValidationError();
                break;
            }
        });
    }

    PacketMetrics PacketStats::snapshot() const
    {
        return PacketMetrics{
            .rx_packets = rx_packets_.load(std::memory_order_relaxed),
            .rx_bytes = rx_bytes_.load(std::memory_order_relaxed),
            .drops_parse_error = drops_parse_error_.load(std::memory_order_relaxed),
            .drops_validation = drops_validation_.load(std::memory_order_relaxed)};
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
        return drops_parse_error_.load(std::memory_order_relaxed) +
               drops_validation_.load(std::memory_order_relaxed);
    }

    void PacketStats::incrementParseError()
    {
        drops_parse_error_.fetch_add(1, std::memory_order_relaxed);
    }

    void PacketStats::incrementValidationError()
    {
        drops_validation_.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace edgenetswitch
