#include "edgenetswitch/replay/ReplayOutcomeCollector.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/Packet.hpp"
#include <mutex>
#include <variant>

namespace edgenetswitch
{
    ReplayOutcomeCollector::ReplayOutcomeCollector(MessagingBus &bus)
    {
        bus.subscribe(MessageType::PacketProcessed,
                      [this](const Message &msg)
                      {
                          const auto *processed = std::get_if<Packet>(&msg.payload);

                          if (processed == nullptr)
                              return;

                          recordProcessed(processed->lifecycle_id);
                      });

        bus.subscribe(MessageType::PacketDropped,
                      [this](const Message &msg)
                      {
                          const auto *dropped = std::get_if<PacketDropped>(&msg.payload);

                          if (dropped == nullptr)
                              return;

                          recordDropped(dropped->lifecycle_id, dropped->reason);
                      });
    }

    std::vector<ReplayOutcome> ReplayOutcomeCollector::snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return outcomes_;
    }

    void ReplayOutcomeCollector::recordProcessed(std::uint64_t lifecycle_id)
    {
        ReplayOutcome outcome{};
        outcome.sequence = next_sequence_.fetch_add(1);
        outcome.type = ReplayOutcomeType::Processed;
        outcome.lifecycle_id = lifecycle_id;
        outcome.drop_reason = PacketDropReason::Unknown;

        std::lock_guard<std::mutex> lock(mutex_);
        outcomes_.push_back(outcome);
    }

    void ReplayOutcomeCollector::recordDropped(std::uint64_t lifecycle_id, PacketDropReason reason)
    {
        ReplayOutcome outcome{};
        outcome.sequence = next_sequence_.fetch_add(1);
        outcome.type = ReplayOutcomeType::Dropped;
        outcome.lifecycle_id = lifecycle_id;
        outcome.drop_reason = reason;

        std::lock_guard<std::mutex> lock(mutex_);
        outcomes_.push_back(outcome);
    }

} // namespace edgenetswitch