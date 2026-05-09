#pragma once

#include <vector>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/replay/ReplayRecord.hpp"

namespace edgenetswitch
{
    class ReplayPlayer
    {
    public:
        explicit ReplayPlayer(MessagingBus &bus);

        void replay(const std::vector<ReplayRecord> &records);

    private:
        MessagingBus &bus_;
    };
} // namespace edgenetswitch