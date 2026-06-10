#include "edgenetswitch/network/UdpReceiver.hpp"

#include <cstring>
#include <iostream>

#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/network/IngressMode.hpp"
#include "edgenetswitch/packet/PacketParser.hpp"
#include "edgenetswitch/packet/PacketValidator.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace edgenetswitch
{
    UdpReceiver::UdpReceiver(MessagingBus &bus, int port, FdRegistry *fd_registry,
                             IngressMode ingress_mode)
        : bus_(bus), port_(port), fd_registry_(fd_registry), ingress_mode_((ingress_mode))
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

        if (ingress_mode_ == IngressMode::NonBlocking)
        {
            // Read existing socket status flags before enabling O_NONBLOCK.
            const int flags = ::fcntl(socket_fd_.get(), F_GETFL, 0);

            if (flags < 0)
            {
                Logger::error("Failed to get socket flags");
                socket_fd_.reset();
                return;
            }

            if (::fcntl(socket_fd_.get(), F_SETFL, flags | O_NONBLOCK) < 0)
            {
                Logger::error("Failed to enable O_NONBLOCK");
                socket_fd_.reset();
                return;
            }

            Logger::info("UDP receiver running in non-blocking mode");
        }
        else
        {
            Logger::info("UDP receiver running in blocking mode");
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

        while (running_)
        {
            handleReadable();
        }
    }

    void UdpReceiver::handleReadable()
    {
        char buffer[1024];
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        ssize_t len = recvfrom(socket_fd_.get(), buffer, sizeof(buffer), 0,
                               (struct sockaddr *)&client_addr, &addr_len);

        if (len < 0)
        {
            if (errno == EBADF)
            {
                running_ = false;
                return; // socket closed, exit thread cleanly
            }

            if (errno == EINTR)
                return;

            // Non-blocking sockets return EAGAIN/EWOULDBLOCK when no packet is available yet.
            // This is an expected runtime condition, not a fatal socket error.
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                Message msg{};
                msg.type = MessageType::IngressIdlePoll;
                msg.timestamp_ms = nowMs();
                msg.payload = IngressIdlePoll{msg.timestamp_ms};
                bus_.publish(std::move(msg));

                return;
            }

            Logger::error("[UDP] recvfrom failed: " + std::string(strerror(errno)));
            return;
        }

        const auto ingress_ts = nowNs();

        std::string data(buffer, static_cast<size_t>(len));
        Logger::info("[UDP] Packet received (" + std::to_string(len) + " bytes)");

        auto lifecycle_id = lifecycle_gen_.next();
        auto packet = parsePacket(data);
        packet.lifecycle_id = lifecycle_id;
        packet.ingress_timestamp_ns = ingress_ts;

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
            return;
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
            return;
        }

        sendto(socket_fd_.get(), buffer, len, 0, (struct sockaddr *)&client_addr, addr_len);

        Message msg{};
        msg.type = MessageType::PacketRx;
        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = std::move(packet);

        bus_.publish(std::move(msg));
    }

    int UdpReceiver::fd() const noexcept
    {
        return socket_fd_.get();
    }
} // namespace edgenetswitch
