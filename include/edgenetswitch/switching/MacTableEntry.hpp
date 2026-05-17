#pragma once

#include "edgenetswitch/switching/MacAddress.hpp"

#include <cstdint>

namespace edgenetswitch
{
    struct MacTableEntry
    {
        MacAddress mac;
        std::uint32_t port_id{0};
        std::uint64_t last_seen_tick{0};
    };
} // namespace edgenetswitch