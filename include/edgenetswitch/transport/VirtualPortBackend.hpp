#pragma once

#include "edgenetswitch/transport/PortBackend.hpp"
#include "edgenetswitch/transport/TransmitResult.hpp"

namespace edgenetswitch::transport
{
    class VirtualPortBackend : public PortBackend
    {
    public:
        TransmitResult transmit(const Packet &packet) override;
    };
} // namespace edgenetswitch::transport