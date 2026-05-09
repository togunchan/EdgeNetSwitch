#pragma once

#include <cstdint>

#include "edgenetswitch/packet/Packet.hpp"

namespace edgenetswitch
{
    struct ReplayRecord
    {
        std::uint64_t sequence;
        Packet packet;
    };
} // namespace edgenetswitch
