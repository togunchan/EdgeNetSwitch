#include "edgenetswitch/replay/ReplayPlayer.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"

namespace edgenetswitch
{
    ReplayPlayer::ReplayPlayer(MessagingBus &bus) : bus_(bus) {}

    void ReplayPlayer::replay(const std::vector<ReplayRecord> &records)
    {
        for (const auto &record : records)
        {
            Message msg;
            msg.type = MessageType::PacketRx;
            msg.timestamp_ms = record.packet.timestamp_ms;
            msg.payload = record.packet;

            bus_.publish(msg);
        }
    }
} // namespace edgenetswitch