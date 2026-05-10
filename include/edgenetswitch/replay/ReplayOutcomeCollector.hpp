#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/replay/ReplayOutcome.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace edgenetswitch
{
    class ReplayOutcomeCollector
    {
    public:
        explicit ReplayOutcomeCollector(MessagingBus &bus);
        std::vector<ReplayOutcome> snapshot() const;

    private:
        void recordProcessed(std::uint64_t lifecycle_id);
        void recordDropped(std::uint64_t lifecycle_id, PacketDropReason reason);

        mutable std::mutex mutex_;
        std::vector<ReplayOutcome> outcomes_;
        std::atomic<std::uint64_t> next_sequence_{0};
    };
} // namespace edgenetswitch