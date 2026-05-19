#pragma once

#include "edgenetswitch/switching/ForwardingDecision.hpp"

#include <cstdint>
#include <vector>

namespace edgenetswitch
{
    struct ForwardingEvent
    {
        std::uint64_t lifecycle_id{0};
        ForwardingAction action{ForwardingAction::Drop};
        std::vector<std::uint32_t> egress_ports{};
    };
} // namespace edgenetswitch