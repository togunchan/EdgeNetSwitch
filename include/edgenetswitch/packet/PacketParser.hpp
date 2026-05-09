#pragma once

#include <string>

#include "edgenetswitch/packet/Packet.hpp"

namespace edgenetswitch
{
    Packet parsePacket(const std::string &data);
} // namespace edgenetswitch
