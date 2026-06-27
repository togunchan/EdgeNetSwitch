#pragma once

#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/transport/PortBackend.hpp"
#include "edgenetswitch/transport/TransmitResult.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace edgenetswitch::transport
{
    class TransportManager
    {
    public:
        void registerBackend(std::uint32_t port_id, std::unique_ptr<PortBackend> backend);
        TransmitResult transmit(std::uint32_t port_id, const Packet& packet);

    private:
        std::unordered_map<std::uint32_t, std::unique_ptr<PortBackend>> backends_;
    };
}; // namespace edgenetswitch::transport
