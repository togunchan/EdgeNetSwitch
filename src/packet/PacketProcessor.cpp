#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"

namespace edgenetswitch
{

    PacketProcessor::PacketProcessor(MessagingBus &bus) : bus_(bus)
    {
        bus_.subscribe(MessageType::PacketRx, [this](const Message &msg)
                       {
                           const Packet *packet = std::get_if<Packet>(&msg.payload);

                           if (!packet)
                               return;

                           std::lock_guard<std::mutex> lock(queue_mutex_);
                           if (queue_.size() >= MAX_QUEUE_SIZE)
                           {
                               Message dropMsg{};
                               dropMsg.type = MessageType::PacketDropped;
                               dropMsg.timestamp_ms = nowMs();
                               dropMsg.payload = PacketDropped{
                                   .reason = PacketDropReason::QueueOverflow,
                                   .timestamp_ms = dropMsg.timestamp_ms,
                                   .packet_id = packet->id};

                               bus_.publish(std::move(dropMsg));
                               return;
                           }
                           queue_.push_back(*packet); });

        worker_ = std::thread([this]()
                              { processLoop(); });
    }

    PacketProcessor::~PacketProcessor()
    {
        running_.store(false, std::memory_order_relaxed);
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void PacketProcessor::processLoop()
    {
        while (running_)
        {
            Packet packet;

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);

                if (queue_.empty())
                    continue;

                packet = std::move(queue_.front());
                queue_.pop_front();
            }

            processPacket(packet);
        }
    }

    void PacketProcessor::processPacket(Packet processedPacket)
    {
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
            return;
        }

        processedPacket.payload_size =
            static_cast<std::uint32_t>(processedPacket.payload.size());

        // Simulate processing cost (CPU / parsing / workload) to test pipeline behavior under load
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        Message processed{};
        processed.type = MessageType::PacketProcessed;
        processed.timestamp_ms = processedPacket.timestamp_ms;
        processed.payload = processedPacket;

        bus_.publish(processed);
    }
} // namespace edgenetswitch
