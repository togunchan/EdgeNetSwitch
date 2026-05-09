#pragma once

#include <atomic>
#include <mutex>
#include <vector>
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/replay/ReplayRecord.hpp"

namespace edgenetswitch
{
    class ReplayRecorder
    {
    public:
        explicit ReplayRecorder(MessagingBus &bus);

        [[nodiscard]]
        std::vector<ReplayRecord> snapshot() const;

    private:
        void onPacketRx(const Message &msg);

        mutable std::mutex mutex_;
        std::vector<ReplayRecord> records_;

        std::atomic<std::uint64_t> next_sequence_{0};
        
    };
} // namespace edgenetswitch