#pragma once

#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/transport/PortBackend.hpp"
#include "edgenetswitch/transport/TransmitResult.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace edgenetswitch::transport
{
    struct TransportCounters
    {
        std::uint64_t tx_packets{0};
        std::uint64_t tx_bytes{0};
        std::uint64_t tx_failed{0};
        std::uint64_t backend_unavailable{0};
        std::uint64_t port_down{0};
        std::uint64_t invalid_packet{0};
    };

    class TransportManager
    {
    public:
        void registerBackend(std::uint32_t port_id, std::unique_ptr<PortBackend> backend);
        TransmitResult transmit(std::uint32_t port_id, const Packet &packet);
        const TransportCounters &counters() const noexcept;
        void resetCounters();

    private:
        std::unordered_map<std::uint32_t, std::unique_ptr<PortBackend>> backends_;
        TransportCounters counters_;
    };
}; // namespace edgenetswitch::transport
