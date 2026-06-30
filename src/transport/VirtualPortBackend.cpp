#include "edgenetswitch/transport/VirtualPortBackend.hpp"
#include "edgenetswitch/core/Logger.hpp"

namespace edgenetswitch::transport
{
    TransmitResult VirtualPortBackend::transmit(const Packet &)
    {
        Logger::debug("VirtualPortBackend: transmit() called");
        return {.status = TransmitStatus::Success};
    }
} // namespace edgenetswitch::transport
