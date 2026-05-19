#pragma once

#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/switching/ForwardingDecision.hpp"
#include "edgenetswitch/switching/InterfaceRegistry.hpp"
#include "edgenetswitch/switching/MacTable.hpp"
#include <cstdint>

namespace edgenetswitch
{
    class SwitchForwardingEngine
    {
    public:
        explicit SwitchForwardingEngine(MacTable &mac_table, InterfaceRegistry &interfaces);

        [[nodiscard]]
        ForwardingDecision processPacket(const Packet &packet, std::uint32_t ingress_port,
                                         std::uint64_t tick);

        const MacTable &macTable() const noexcept;

    private:
        MacTable &mac_table_;
        InterfaceRegistry& interfaces_;
    };

} // namespace edgenetswitch
