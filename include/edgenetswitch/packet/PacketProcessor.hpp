#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"

namespace edgenetswitch
{

    class PacketProcessor
    {
    public:
        explicit PacketProcessor(MessagingBus &bus);

    private:
        MessagingBus &bus_;
    };

}
