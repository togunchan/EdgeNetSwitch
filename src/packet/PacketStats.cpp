#include "edgenetswitch/packet/PacketStats.hpp"

namespace edgenetswitch
{
    PacketStats::PacketStats(MessagingBus &bus)
    {
        bus.subscribe(MessageType::PacketRx, [this](const Message &msg)
                      {
                          const Packet &p = std::get<Packet>(msg.payload);

                          rx_packets_++;
                          rx_bytes_ = p.size_bytes; });
    }

    std::uint64_t PacketStats::rxPackets() const
    {
        return rx_packets_;
    }

    std::uint64_t PacketStats::rxBytes() const
    {
        return rx_bytes_;
    }

    std::uint64_t PacketStats::drops() const
    {
        return drops_;
    }
} // namespace edgenetswitch
