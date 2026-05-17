#pragma once

#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/switching/ForwardingDecision.hpp"
#include "edgenetswitch/switching/MacTable.hpp"
#include <cstdint>
#include <vector>

namespace edgenetswitch
{
    class SwitchForwardingEngine
    {
    public:
        explicit SwitchForwardingEngine(MacTable &mac_table);

        [[nodiscard]]
        ForwardingDecision processPacket(const Packet &packet, std::uint32_t ingress_port,
                                         std::uint64_t tick,
                                         const std::vector<std::uint32_t> &available_ports);

    private:
        MacTable &mac_table_;
    };

}// namespace edgenetswitch
