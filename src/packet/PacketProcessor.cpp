#include "edgenetswitch/packet/PacketProcessor.hpp"

namespace edgenetswitch
{
    PacketProcessor::PacketProcessor(MessagingBus &bus) : bus_(bus)
    {
        bus_.subscribe(MessageType::PacketRx, [this](const Message &msg)
                       {
                           const Packet* packet = std::get_if<Packet>(&msg.payload);
                           if (!packet)
                           {
                               return;
                           }

                           Message processed{};
                           processed.type = MessageType::PacketProcessed;
                           processed.timestamp_ms = msg.timestamp_ms;
                           processed.payload = *packet;

                           bus_.publish(processed); });
    }
} // namespace edgenetswitch
