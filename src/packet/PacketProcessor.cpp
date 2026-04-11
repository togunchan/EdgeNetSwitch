#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"

namespace edgenetswitch
{
    static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;

    PacketProcessor::PacketProcessor(MessagingBus &bus) : bus_(bus)
    {
        bus_.subscribe(MessageType::PacketRx, [this](const Message &msg)
                       {
                           const Packet* packet = std::get_if<Packet>(&msg.payload);
                           if (!packet)
                           {
                               return;
                           }

                           Packet processedPacket = *packet;

                           if (processedPacket.payload.size() > MAX_PAYLOAD_SIZE)
                           {
                               Message dropMsg{};
                               dropMsg.type = MessageType::PacketDropped;
                               dropMsg.timestamp_ms = processedPacket.timestamp_ms;
                               dropMsg.payload = PacketDropped{
                                   .reason = PacketDropReason::ValidationError,
                                   .timestamp_ms = processedPacket.timestamp_ms,
                                   .packet_id = processedPacket.id};

                               bus_.publish(std::move(dropMsg));

                               Logger::warn("[DROP][PROCESSOR][VALIDATION] payload too large");
                               return;
                           }

                           if (processedPacket.timestamp_ms == 0)
                           {
                               Message dropMsg{};
                               dropMsg.type = MessageType::PacketDropped;
                               dropMsg.timestamp_ms = nowMs();
                               dropMsg.payload = PacketDropped{
                                   .reason = PacketDropReason::ValidationError,
                                   .timestamp_ms = dropMsg.timestamp_ms,
                                   .packet_id = processedPacket.id};

                               bus_.publish(std::move(dropMsg));

                               Logger::warn("[DROP][PROCESSOR][VALIDATION] invalid timestamp");
                               return;
                           }

                           processedPacket.payload_size =
                               static_cast<std::uint32_t>(processedPacket.payload.size());

                           Logger::debug("[PROCESSOR] Packet processed: id=" +
                                        std::to_string(processedPacket.id) +
                                        ", payload_size=" +
                                        std::to_string(processedPacket.payload_size));

                           Message processed{};
                           processed.type = MessageType::PacketProcessed;
                           processed.timestamp_ms = msg.timestamp_ms;
                           processed.payload = processedPacket;

                           bus_.publish(processed); });
    }
} // namespace edgenetswitch
