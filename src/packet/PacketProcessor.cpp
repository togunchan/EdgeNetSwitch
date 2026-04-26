#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"

#include <utility>

namespace edgenetswitch
{
    PacketProcessor::PacketProcessor(MessagingBus &bus)
        : PacketProcessor(bus, failure::FailureInjector{failure::FailureConfig{}})
    {
    }

    PacketProcessor::PacketProcessor(MessagingBus &bus, failure::FailureInjector injector) : bus_(bus), injector_(std::move(injector))
    {
        bus_.subscribe(MessageType::PacketRx, [this](const Message &msg)
                       {
                           const Packet *packet = std::get_if<Packet>(&msg.payload);

                           if (!packet)
                               return;
                           const auto now_ms = nowMs();
                           auto failure = injector_.inject(*packet, now_ms);

                           if(failure.is_terminal){
                               handleInjectedFailure(*packet, failure, now_ms);
                               return;
                           }

                           bool shouldDrop = false;
                           {
                               std::lock_guard<std::mutex> lock(queue_mutex_);
                               if (queue_.size() >= MAX_QUEUE_SIZE)
                               {
                                   shouldDrop = true;
                               }
                               else {
                                   queue_.push_back(*packet);
                               }
                           }

                           if(shouldDrop){
                               Message dropMsg{};
                               dropMsg.type = MessageType::PacketDropped;
                               dropMsg.timestamp_ms = now_ms;
                               dropMsg.payload = PacketDropped{
                                   .reason = PacketDropReason::QueueOverflow,
                                   .timestamp_ms = dropMsg.timestamp_ms,
                                   .packet_id = packet->id,
                                   .lifecycle_id = packet->lifecycle_id};

                               bus_.publish(std::move(dropMsg));
                           }
                           else {
                           cv_.notify_one();
                           } });

        worker_ = std::thread([this]()
                              { processLoop(); });
    }

    PacketProcessor::~PacketProcessor()
    {
        running_.store(false, std::memory_order_relaxed);
        cv_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }

    void PacketProcessor::processLoop()
    {
        while (true)
        {
            Packet packet;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                cv_.wait(lock, [this]
                         { 
                            // Continue waiting only while queue is empty AND system is running.
                            return !queue_.empty() || !running_; });

                if (!running_ && queue_.empty())
                    break;

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
                .packet_id = processedPacket.id,
                .lifecycle_id = processedPacket.lifecycle_id};

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
                .packet_id = processedPacket.id,
                .lifecycle_id = processedPacket.lifecycle_id};

            bus_.publish(std::move(dropMsg));
            return;
        }

        processedPacket.payload_size =
            static_cast<std::uint32_t>(processedPacket.payload.size());

        // Simulate processing cost (CPU / parsing / workload) to test pipeline behavior under load
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));

        Message processed{};
        processed.type = MessageType::PacketProcessed;
        processed.timestamp_ms = processedPacket.timestamp_ms;
        processed.payload = processedPacket;

        bus_.publish(processed);
    }

    void PacketProcessor::handleInjectedFailure(const Packet &pkt, const failure::FailureResult &failure, std::uint64_t now_ms)
    {
        PacketDropReason reason;

        switch (failure.type)
        {
        case failure::FailureType::MalformedPacket:
            reason = PacketDropReason::ParseError;
            break;
        case failure::FailureType::ValidationError:
            reason = PacketDropReason::ValidationError;
            break;
        case failure::FailureType::SimulatedLoss:
            reason = PacketDropReason::SimulatedLoss;
            break;
        case failure::FailureType::ProcessingRejection:
            reason = PacketDropReason::ProcessingError;
            break;
        default:
            reason = PacketDropReason::InternalError;
            break;
        }

        Message dropMsg{};
        dropMsg.type = MessageType::PacketDropped;
        dropMsg.timestamp_ms = now_ms;
        dropMsg.payload = PacketDropped{
            .reason = reason,
            .timestamp_ms = dropMsg.timestamp_ms,
            .packet_id = pkt.id,
            .lifecycle_id = pkt.lifecycle_id};

        bus_.publish(std::move(dropMsg));
    }

} // namespace edgenetswitch
