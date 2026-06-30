#include "edgenetswitch/transport/UdpPortBackend.hpp"
#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"
#include "edgenetswitch/system/fd/FileDescriptor.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>

namespace edgenetswitch::transport
{
    UdpPortBackend::UdpPortBackend(std::uint32_t port_id, const UdpEndpoint &endpoint,
                                   FdRegistry *fd_registry)
        : port_id_(port_id), endpoint_(endpoint),
          socket_(::socket(AF_INET, SOCK_DGRAM, 0), fd_registry, FdType::UdpSocket)
    {
        if (!socket_.valid())
        {
            throw std::system_error(errno, std::system_category(), "Failed to create UDP socket");
        }

        destination_ = {};
        destination_.sin_family = AF_INET;
        destination_.sin_port = htons(endpoint_.port);

        if (::inet_pton(AF_INET, endpoint_.ip.c_str(), &destination_.sin_addr) != 1)
        {
            throw std::invalid_argument("Invalid IPv4 address: " + endpoint_.ip);
        }
    }

    TransmitResult UdpPortBackend::transmit(const Packet &packet)
    {
        if (packet.payload.empty())
        {
            return {.status = TransmitStatus::Success, .port_id = port_id_, .bytes_transmitted = 0};
        }

        const ssize_t bytes_sent =
            ::sendto(socket_.get(), packet.payload.data(), packet.payload.size(), 0,
                     reinterpret_cast<const sockaddr *>(&destination_), sizeof(destination_));

        if (bytes_sent < 0)
        {
            return {.status = TransmitStatus::SendFailed,
                    .bytes_transmitted = 0,
                    .native_error = errno};
        }

        Logger::info("UdpPortBackend: transmitted " + std::to_string(bytes_sent) + " bytes to " +
                     endpoint_.ip + ":" + std::to_string(endpoint_.port));

        return {.status = TransmitStatus::Success,
                .port_id = port_id_,
                .bytes_transmitted = static_cast<std::uint32_t>(bytes_sent)};
    }
} // namespace edgenetswitch::transport