#include "edgenetswitch/switching/InterfaceRegistry.hpp"

#include <utility>

namespace edgenetswitch
{
    void InterfaceRegistry::addPort(SwitchPort port)
    {
        ports_.emplace(port.id(), std::move(port));
    }

    std::optional<SwitchPort> InterfaceRegistry::findPort(std::uint32_t port_id) const
    {
        auto it = ports_.find(port_id);

        if (it == ports_.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    bool InterfaceRegistry::isUp(std::uint32_t port_id) const
    {
        auto port = findPort(port_id);

        if (!port)
        {
            return false;
        }

        return port->state() == PortState::Up;
    }

    void InterfaceRegistry::setState(std::uint32_t port_id, PortState state)
    {
        auto it = ports_.find(port_id);

        if (it == ports_.end())
        {
            return;
        }

        it->second.setState(state);
    }

    std::vector<std::uint32_t> InterfaceRegistry::activePortIds() const
    {
        std::vector<std::uint32_t> active_ports;

        for (const auto &[id, port] : ports_)
        {
            if (port.state() == PortState::Up)
            {
                active_ports.push_back(id);
            }
        }

        return active_ports;
    }

    std::vector<SwitchPort> InterfaceRegistry::snapshot() const
    {
        std::vector<SwitchPort> snapshot;
        snapshot.reserve(ports_.size());

        for (const auto &entry_pair : ports_)
        {
            snapshot.push_back(entry_pair.second);
        }

        return snapshot;
    }
} // namespace edgenetswitch