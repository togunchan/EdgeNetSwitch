
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
            ++counters_.backend_unavailable;
            ++counters_.tx_failed;
            return {.status = TransmitStatus::BackendUnavailable, .port_id = port_id};
        }

        auto result = it->second->transmit(packet);

        switch (result.status)
        {
        case TransmitStatus::Success:
            ++counters_.tx_packets;
            counters_.tx_bytes += result.bytes_transmitted;
            break;
        case TransmitStatus::PortDown:
            ++counters_.tx_failed;
            ++counters_.port_down;
            break;
        case TransmitStatus::BackendUnavailable:
            ++counters_.tx_failed;
            ++counters_.backend_unavailable;
            break;
        case TransmitStatus::InvalidPacket:
            ++counters_.tx_failed;
            ++counters_.invalid_packet;
            break;
        case TransmitStatus::SendFailed:
            ++counters_.tx_failed;
            break;
        default:
            ++counters_.tx_failed;
            break;
        }

        return result;
    }

    const TransportCounters &TransportManager::counters() const noexcept
    {
        return counters_;
    }

    void TransportManager::resetCounters()
    {
        counters_ = {};
    }
} // namespace edgenetswitch::transport
