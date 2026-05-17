#pragma once

#include <cstdint>
#include <vector>

namespace edgenetswitch
{
    enum class ForwardingAction
    {
        Drop,
        Flood,
        ForwardToPorts
    };

    struct ForwardingDecision
    {
        ForwardingAction action{ForwardingAction::Drop};

        std::vector<std::uint32_t> egress_ports{};
    };
} // namespace edgenetswitch