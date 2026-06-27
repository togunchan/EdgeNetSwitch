
#include "edgenetswitch/transport/TransportManager.hpp"
#include "edgenetswitch/transport/TransmitResult.hpp"
#include <utility>

namespace edgenetswitch::transport
{
    void TransportManager::registerBackend(std::uint32_t port_id,
                                           std::unique_ptr<PortBackend> backend)
    {
        backends_[port_id] = std::move(backend);
    }

    TransmitResult TransportManager::transmit(std::uint32_t port_id, const Packet &packet)
    {
        auto it = backends_.find(port_id);

        if (it == backends_.end())
        {
            return
            {
                .status = TransmitStatus::BackendUnavailable,
                .port_id = port_id
            };
        }

        return it->second->transmit(packet);
    }
} // namespace edgenetswitch::transport
