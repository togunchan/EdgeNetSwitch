#include "edgenetswitch/replay/ReplayRecorder.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include <atomic>
#include <mutex>

namespace edgenetswitch
{
    ReplayRecorder::ReplayRecorder(MessagingBus &bus)
    {
        bus.subscribe(MessageType::PacketRx, [&](const Message &msg) { onPacketRx((msg)); });
    }

    std::vector<ReplayRecord> ReplayRecorder::snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }

    void ReplayRecorder::onPacketRx(const Message &msg)
    {
        const auto *packet = std::get_if<Packet>(&msg.payload);

        if (packet == nullptr)
            return;

        const auto sequence = next_sequence_.fetch_add(1, std::memory_order_relaxed);

        ReplayRecord record = {.sequence = sequence, .packet = *packet};

        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back(std::move(record));
    }

} // namespace edgenetswitch