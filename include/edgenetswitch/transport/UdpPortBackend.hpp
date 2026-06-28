#pragma once

#include "edgenetswitch/system/fd/FileDescriptor.hpp"
#include "edgenetswitch/transport/PortBackend.hpp"
#include "edgenetswitch/transport/TransmitResult.hpp"

#include <cstdint>
#include <netinet/in.h>
#include <string>
namespace edgenetswitch::transport
{

    struct UdpEndpoint
    {
        std::string ip;
        std::uint16_t port;
    };

    class UdpPortBackend final : public PortBackend
    {
    public:
        UdpPortBackend(std::uint32_t port_id, const UdpEndpoint &endpoint, FdRegistry *registry);
        TransmitResult transmit(const Packet &packet) override;

    private:
        std::uint32_t port_id_;
        UdpEndpoint endpoint_;
        FileDescriptor socket_;
        sockaddr_in destination_{};
    };
} // namespace edgenetswitch::transport
