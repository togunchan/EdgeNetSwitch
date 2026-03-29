#include "edgenetswitch/packet/PacketGenerator.hpp"

namespace edgenetswitch
{
    PacketGenerator::PacketGenerator(MessagingBus &bus) : bus_(bus) {};

    void PacketGenerator::onTick(std::uint64_t now_ms)
    {
        Packet packet;
        packet.id = packet_id_++;
        packet.timestamp_ms = now_ms;
        packet.payload_size = 64;

        Message msg;
        msg.type = MessageType::PacketRx;
        msg.timestamp_ms = now_ms;
        msg.payload = packet;

        bus_.publish(msg);
    }

} // namespace edgenetswitch