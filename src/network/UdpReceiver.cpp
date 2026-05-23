#include "edgenetswitch/network/UdpReceiver.hpp"

#include <cstring>
#include <iostream>

#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"
#include "edgenetswitch/packet/PacketParser.hpp"
#include "edgenetswitch/packet/PacketValidator.hpp"
#include "edgenetswitch/system/FdRegistry.hpp"
#include "edgenetswitch/system/FdType.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace edgenetswitch
{
    UdpReceiver::UdpReceiver(MessagingBus &bus, int port, FdRegistry *fd_registry)
        : bus_(bus), port_(port), fd_registry_(fd_registry)
    {
    }

    UdpReceiver::~UdpReceiver()
    {
        stop();
    }

    void UdpReceiver::start()
    {
        if (running_)
            return;

        // Create UDP socket
        const int raw_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (raw_fd < 0)
        {
            std::cerr << "Failed to create socket\n";
            return;
        }

        socket_fd_ = FileDescriptor(raw_fd, fd_registry_, FdType::UdpSocket);

        // Bind to port
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(socket_fd_.get(), (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            std::cerr << "Failed to bind socket\n";
            socket_fd_.reset();
            return;
        }

        running_ = true;

        // Start worker thread
        worker_ = std::thread(&UdpReceiver::run, this);

        std::cout << "[UDP] Listening on port " << port_ << "\n";
    }

    void UdpReceiver::stop()
    {
        if (!running_)
            return;

        running_ = false;

        if (socket_fd_.valid())
        {
            socket_fd_.reset();
        }

        if (worker_.joinable())
        {
            worker_.join();
        }

        std::cout << "[UDP] Stopped\n";
    }

    void UdpReceiver::run()
    {
        char buffer[1024];

        while (running_)
        {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);

            ssize_t len = recvfrom(socket_fd_.get(), buffer, sizeof(buffer), 0,
                                   (struct sockaddr *)&client_addr, &addr_len);

            if (len < 0)
            {
                if (errno == EBADF)
                    break; // socket closed, exit thread cleanly

                if (errno == EINTR)
                    continue;

                Logger::error("[UDP] recvfrom failed: " + std::string(strerror(errno)));
                continue;
            }

            std::string data(buffer, static_cast<size_t>(len));
            Logger::info("[UDP] Packet received (" + std::to_string(len) + " bytes)");

            auto lifecycle_id = lifecycle_gen_.next();
            auto packet = parsePacket(data);
            packet.lifecycle_id = lifecycle_id;

            if (!packet.valid)
            {
                const auto ts = nowMs();

                Message dropMsg{};
                dropMsg.type = MessageType::PacketDropped;
                dropMsg.timestamp_ms = ts;
                dropMsg.payload = PacketDropped{.reason = PacketDropReason::ParseError,
                                                .timestamp_ms = ts,
                                                .packet_id = 0,
                                                .lifecycle_id = lifecycle_id};

                bus_.publish(std::move(dropMsg));
                Logger::warn("[DROP][UDP][PARSE] Packet rejected: " + data);
                continue;
            }
            packet.timestamp_ms = nowMs();
            packet.wire_size = static_cast<std::uint32_t>(len);

            // inet_ntoa() uses a static internal buffer (not thread-safe),
            // but we immediately copy into std::string, so it's safe here.
            packet.source_ip = inet_ntoa(client_addr.sin_addr);
            // Convert port from network byte order (big-endian) to host byte order.
            // Network protocols always use big-endian, but the host machine
            // (e.g. x86) is typically little-endian.
            packet.source_port = ntohs(client_addr.sin_port);

            auto result = PacketValidator::validate(packet);

            if (!result.accepted)
            {
                const auto ts = nowMs();

                Message dropMsg{};
                dropMsg.type = MessageType::PacketDropped;
                dropMsg.timestamp_ms = ts;
                dropMsg.payload = PacketDropped{.reason = PacketDropReason::ValidationError,
                                                .timestamp_ms = ts,
                                                .packet_id = packet.id,
                                                .lifecycle_id = lifecycle_id};

                bus_.publish(std::move(dropMsg));
                Logger::warn("[DROP][UDP][VALIDATION] Packet rejected: reason=" +
                             toString(result.reason));
                continue;
            }

            sendto(socket_fd_.get(), buffer, len, 0, (struct sockaddr *)&client_addr, addr_len);

            Message msg{};
            msg.type = MessageType::PacketRx;
            msg.timestamp_ms = packet.timestamp_ms;
            msg.payload = std::move(packet);

            bus_.publish(std::move(msg));
        }
    }
} // namespace edgenetswitch
