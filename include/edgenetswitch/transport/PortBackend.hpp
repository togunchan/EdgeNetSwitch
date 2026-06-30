#pragma once

#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/transport/TransmitResult.hpp"

namespace edgenetswitch::transport
{
    class PortBackend
    {
    public:
        virtual ~PortBackend() = default;

        virtual TransmitResult transmit(const Packet &packet) = 0;
    };
}; // namespace edgenetswitch::transport
