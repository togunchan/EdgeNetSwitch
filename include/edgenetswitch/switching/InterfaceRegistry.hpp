#pragma once

#include "edgenetswitch/switching/SwitchPort.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace edgenetswitch
{
    class InterfaceRegistry
    {
    public:
        void addPort(SwitchPort port);

        [[nodiscard]]
        std::optional<SwitchPort> findPort(std::uint32_t port_id) const;

        [[nodiscard]]
        bool isUp(std::uint32_t port_id) const;

        void setState(std::uint32_t port_id, PortState state);

        [[nodiscard]]
        std::vector<std::uint32_t> activePortIds() const;

        [[nodiscard]]
        std::vector<SwitchPort> snapshot() const;

    private:
        std::map<std::uint32_t, SwitchPort> ports_;
    };
} // namespace edgenetswitch