#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"

namespace edgenetswitch
{
    class PacketGenerator
    {
    public:
        PacketGenerator(MessagingBus &bus);

        void onTick(std::uint64_t now_ms);

    private:
        MessagingBus &bus_;
        std::uint64_t packet_id_{0};
    };

} // namespace edgenetswitch
