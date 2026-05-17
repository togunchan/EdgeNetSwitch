#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/switching/ForwardingDecision.hpp"
#include "edgenetswitch/switching/InterfaceRegistry.hpp"

namespace edgenetswitch
{
    SwitchForwardingEngine::SwitchForwardingEngine(MacTable &mac_table,
                                                   InterfaceRegistry &interfaces)
        : mac_table_(mac_table), interfaces_(interfaces)
    {
    }

    ForwardingDecision SwitchForwardingEngine::processPacket(const Packet &packet,
                                                             std::uint32_t ingress_port,
                                                             std::uint64_t tick)
    {
        if (!packet.source_mac || !packet.destination_mac)
            return {};

        mac_table_.learn(*packet.source_mac, ingress_port, tick);

        ForwardingDecision decision{};
        if (packet.destination_mac->isBroadcast())
        {
            decision.action = ForwardingAction::Flood;

            for (std::uint32_t port : interfaces_.activePortIds())
            {
                if (port == ingress_port)
                {
                    continue;
                }
                decision.egress_ports.push_back(port);
            }
            return decision;
        }

        auto destination_port = mac_table_.lookup(*packet.destination_mac);

        if (destination_port)
        {
            if (!interfaces_.isUp(*destination_port))
            {
                decision.action = ForwardingAction::Drop;
                return decision;
            }

            decision.action = ForwardingAction::ForwardToPorts;
            decision.egress_ports.push_back(*destination_port);

            return decision;
        }

        decision.action = ForwardingAction::Flood;

        for (std::uint32_t port : interfaces_.activePortIds())
        {
            if (port == ingress_port)
            {
                continue;
            }
            decision.egress_ports.push_back(port);
        }

        return decision;
    }

} // namespace edgenetswitch