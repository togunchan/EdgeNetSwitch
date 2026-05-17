#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/switching/ForwardingDecision.hpp"

namespace edgenetswitch
{
    SwitchForwardingEngine::SwitchForwardingEngine(MacTable &mac_table) : mac_table_(mac_table) {}

    ForwardingDecision
    SwitchForwardingEngine::processPacket(const Packet &packet, std::uint32_t ingress_port,
                                          std::uint64_t tick,
                                          const std::vector<std::uint32_t> &available_ports)
    {
        if (!packet.source_mac || !packet.destination_mac)
            return {};

        mac_table_.learn(*packet.source_mac, ingress_port, tick);

        ForwardingDecision decision{};
        if (packet.destination_mac->isBroadcast())
        {
            decision.action = ForwardingAction::Flood;

            for (std::uint32_t port : available_ports)
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
            decision.action = ForwardingAction::ForwardToPorts;
            decision.egress_ports.push_back(*destination_port);

            return decision;
        }

        decision.action = ForwardingAction::Flood;

        for (std::uint32_t port : available_ports)
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