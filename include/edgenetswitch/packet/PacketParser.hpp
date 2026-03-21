#pragma once

#include <string>
#include "edgenetswitch/messaging/MessagingBus.hpp"

namespace edgenetswitch
{
    Packet parsePacket(const std::string &data);
} // namespace edgenetswitch
