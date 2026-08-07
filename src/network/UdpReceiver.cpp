#include "edgenetswitch/network/UdpReceiver.hpp"

#include <array>
#include <asm-generic/socket.h>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <fcntl.h>
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
#include <sstream>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace edgenetswitch
{
    namespace
    {
        void logMsgFlags(int flags)
        {
            if (flags == 0)
            {
                Logger::debug("[UDP] No message flags");
                return;
            }

            if (flags & MSG_TRUNC)
            {
                Logger::info("[UDP] MSG_TRUNC");
            }

            if (flags & MSG_CTRUNC)
            {
                Logger::info("[UDP] MSG_CTRUNC");
            }

            if (flags & MSG_OOB)
            {
                Logger::info("[UDP] MSG_OOB");
            }

            if (flags & MSG_EOR)
            {
                Logger::info("[UDP] MSG_EOR");
            }
        }

        std::uint64_t timespecToNanoseconds(const timespec &timestamp)
        {
            constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

            return static_cast<std::uint64_t>(timestamp.tv_sec) * nanoseconds_per_second +
                   static_cast<std::uint64_t>(timestamp.tv_nsec);
        }

        std::optional<std::uint64_t> extractKernelReceiveTimestampNs(msghdr &message)
        {
            for (cmsghdr *control = CMSG_FIRSTHDR(&message); control != nullptr;
                 control = CMSG_NXTHDR(&message, control))
            {
                Logger::debug(
                    "[UDP] control message: level=" + std::to_string(control->cmsg_level) +
                    ", type=" + std::to_string(control->cmsg_type) +
                    ", len=" + std::to_string(control->cmsg_len));

                if (control->cmsg_level != SOL_SOCKET)
                {
                    continue;
                }

                if (control->cmsg_type != SCM_TIMESTAMPNS)
                {
                    continue;
                }

                if (control->cmsg_len < CMSG_LEN(sizeof(timespec)))
                {
                    Logger::warn("[UDP] Invalid SCM_TIMESTAMPNS control message");
                    return std::nullopt;
                }

                const auto *timestamp = reinterpret_cast<const timespec *>(CMSG_DATA(control));

                Logger::debug("[UDP] kernel timestamp: sec=" + std::to_string(timestamp->tv_sec) +
                              ", nsec=" + std::to_string(timestamp->tv_nsec));

                return timespecToNanoseconds(*timestamp);
            }

            return std::nullopt;
        }

        std::optional<std::uint32_t> extractKernelReceiveDropCount(msghdr &message)
        {
            for (cmsghdr *control = CMSG_FIRSTHDR(&message); control != nullptr;
                 control = CMSG_NXTHDR(&message, control))
            {
                if (control->cmsg_level != SOL_SOCKET)
                {
                    // Logger::debug("[UDP] Not a SOL_SOCKET control message");
                    continue;
                }

                if (control->cmsg_type != SO_RXQ_OVFL)
                {
                    // Logger::debug("[UDP] Not an SO_RXQ_OVFL control message");
                    continue;
                }

                if (control->cmsg_len < CMSG_LEN(sizeof(std::uint32_t)))
                {
                    Logger::warn("[UDP] Invalid SO_RXQ_OVFL control message");

                    return std::nullopt;
                }

                const auto *drop_count =
                    reinterpret_cast<const std::uint32_t *>(CMSG_DATA(control));

                Logger::debug("[UDP] kernel receive queue drops=" + std::to_string(*drop_count));

                return *drop_count;
            }

            return std::nullopt;
        }
    } // anonymous namespace

    UdpReceiver::UdpReceiver(MessagingBus &bus, std::uint32_t switchPort, std::uint16_t listenPort,
                             LifecycleIdGenerator &lifecycle_gen, FdRegistry *fd_registry,
                             IngressMode ingress_mode)
        : bus_(bus), switchPort_(switchPort), listenPort_(listenPort),
          lifecycle_gen_(lifecycle_gen), fd_registry_(fd_registry), ingress_mode_(ingress_mode)
    {
    }

    UdpReceiver::~UdpReceiver()
    {
        stop();
    }

    void UdpReceiver::initializeSocket()
    {
        // Create UDP socket
        const int raw_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (raw_fd < 0)
        {
            std::cerr << "Failed to create socket\n";
            return;
        }

        socket_fd_ = FileDescriptor(raw_fd, fd_registry_, FdType::UdpSocket);

        // Activate SO_TIMESTAMPNS
        int enable_timestamping = 1;

        if (::setsockopt(socket_fd_.get(), SOL_SOCKET, SO_TIMESTAMPNS, &enable_timestamping,
                         sizeof(enable_timestamping)) < 0)
        {
            Logger::error("[UDP] Failed to enable SO_TIMESTAMPNS: " +
                          std::string(std::strerror(errno)));

            throw std::runtime_error("Failed to enable UDP receive timestamping");
        }
        Logger::debug("[UDP] SO_TIMESTAMPNS enabled");

        // Activate SO_RXQ_OVFL
        int enable_rxq_overflow = 1;

        if (::setsockopt(socket_fd_.get(), SOL_SOCKET, SO_RXQ_OVFL, &enable_rxq_overflow,
                         sizeof(enable_rxq_overflow)) < 0)
        {
            Logger::error("[UDP] Failed to enable SO_RXQ_OVFL: " +
                          std::string(std::strerror(errno)));

            throw std::runtime_error("Failed to enable UDP receive queue overflow reporting");
        }
        Logger::debug("[UDP] SO_RXQ_OVFL enabled");

        // Read the effective UDP receive buffer size applied by the kernel.
        socklen_t option_length = sizeof(receive_buffer_bytes_);

        if (::getsockopt(socket_fd_.get(), SOL_SOCKET, SO_RCVBUF, &receive_buffer_bytes_,
                         &option_length) < 0)
        {
            Logger::error("[UDP] Failed to read SO_RCVBUF: " + std::string(std::strerror(errno)));

            throw std::runtime_error("Failed to read UDP receive buffer size");
        }

        Logger::info("[UDP] effective SO_RCVBUF=" + std::to_string(receive_buffer_bytes_) +
                     " bytes");

        // Bind to port
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(listenPort_);

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
    }

    void UdpReceiver::start()
    {
        if (running_)
            return;

        initializeSocket();

        running_ = true;

        // Start worker thread
        worker_ = std::thread(&UdpReceiver::run, this);

        std::cout << "[UDP] Listening on port " << listenPort_ << " for switch port " << switchPort_
                  << "\n";
    }

    void UdpReceiver::stop()
    {
        if (!running_ && !socket_fd_.valid() && !worker_.joinable())
        {
            return;
        }

        Logger::info("[UDP][SHUTDOWN] Stop requested");

        running_ = false;

        if (socket_fd_.valid())
        {
            socket_fd_.reset();
            Logger::debug("[UDP][SHUTDOWN] Socket closed");
        }

        if (worker_.joinable())
        {
            Logger::info("[UDP][SHUTDOWN] Waiting for worker thread");
            worker_.join();
            Logger::info("[UDP][SHUTDOWN] Worker thread stopped");
        }

        Logger::info("[UDP][SHUTDOWN] UDP receiver stopped");
    }

    void UdpReceiver::run()
    {

        while (running_)
        {
            handleReadable();
        }
    }

    UdpReceiveStatus UdpReceiver::handleReadable()
    {
        std::array<char, UDP_RECEIVE_BUFFER_SIZE> buffer{};
        iovec io{};
        io.iov_base = buffer.data();
        io.iov_len = buffer.size();

        sockaddr_in client_addr{};

        // Reserve ancillary-data space for the kernel receive timestamp
        // and the 32-bit receive queue overflow counter.
        constexpr std::size_t control_buffer_size =
            CMSG_SPACE(sizeof(timespec)) + CMSG_SPACE(sizeof(std::uint32_t));

        std::array<std::byte, control_buffer_size> control_buffer{};

        msghdr message{};
        message.msg_name = &client_addr;
        message.msg_namelen = sizeof(client_addr);
        message.msg_iov = &io;
        message.msg_iovlen = 1;
        message.msg_control = control_buffer.data();
        message.msg_controllen = control_buffer.size();

        const ssize_t len = ::recvmsg(socket_fd_.get(), &message, 0);

        if (len < 0)
        {
            if (errno == EBADF)
            {
                running_ = false;
                return UdpReceiveStatus::Closed; // socket closed, exit thread cleanly
            }

            if (errno == EINTR)
                return UdpReceiveStatus::Interrupted;

            // Non-blocking sockets return EAGAIN/EWOULDBLOCK when no packet is available yet.
            // This is an expected runtime condition, not a fatal socket error.
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                Message msg{};
                msg.type = MessageType::IngressIdlePoll;
                msg.timestamp_ms = nowMs();
                msg.payload = IngressIdlePoll{msg.timestamp_ms};
                bus_.publish(std::move(msg));

                return UdpReceiveStatus::NoDataAvailable;
            }

            Logger::error("[UDP] recvfrom failed: " + std::string(strerror(errno)));
            return UdpReceiveStatus::Error;
        }

        const auto userspace_receive_realtime_ns = nowRealtimeNs();

        std::ostringstream flags_stream;
        flags_stream << std::hex << message.msg_flags;

        Logger::debug("[UDP] recvmsg: len=" + std::to_string(len) + ", flags=0x" +
                      flags_stream.str());
        logMsgFlags(message.msg_flags);

        if ((message.msg_flags & MSG_TRUNC) != 0)
        {
            const auto ts = nowMs();
            const auto lifecycle_id = lifecycle_gen_.next();

            Message dropMsg{};
            dropMsg.type = MessageType::PacketDropped;
            dropMsg.timestamp_ms = ts;
            dropMsg.payload = PacketDropped{.reason = PacketDropReason::DatagramTruncated,
                                            .timestamp_ms = ts,
                                            .packet_id = 0,
                                            .lifecycle_id = lifecycle_id};

            bus_.publish(std::move(dropMsg));

            Logger::warn("[DROP][UDP][TRUNCATED] Datagram exceeded receive buffer capacity");

            return UdpReceiveStatus::DatagramReceived;
        }

        const auto kernel_receive_realtime_ns = extractKernelReceiveTimestampNs(message);
        const auto kernel_receive_drop_count = extractKernelReceiveDropCount(message);

        std::optional<std::uint64_t> kernel_to_userspace_receive_latency_ns;

        if (kernel_receive_realtime_ns &&
            userspace_receive_realtime_ns >= *kernel_receive_realtime_ns)
        {
            kernel_to_userspace_receive_latency_ns =
                userspace_receive_realtime_ns - *kernel_receive_realtime_ns;
        }

        const auto ingress_ts = nowNs();

        std::string data(buffer.data(), static_cast<size_t>(len));
        Logger::info("[UDP] Packet received (" + std::to_string(len) + " bytes)");

        auto lifecycle_id = lifecycle_gen_.next();
        auto packet = parsePacket(data);
        packet.lifecycle_id = lifecycle_id;
        packet.ingress_timestamp_ns = ingress_ts;
        packet.kernel_receive_realtime_ns = kernel_receive_realtime_ns;
        packet.kernel_receive_drop_count = kernel_receive_drop_count;
        packet.kernel_to_userspace_receive_latency_ns = kernel_to_userspace_receive_latency_ns;
        packet.ingress_port = switchPort_;

        if (packet.kernel_to_userspace_receive_latency_ns)
        {
            Logger::debug("[UDP] packet kernel-to-userspace latency=" +
                          std::to_string(*packet.kernel_to_userspace_receive_latency_ns) + " ns");
        }

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
            Logger::warn("[DROP][UDP][PARSE] len=" + std::to_string(len) + " data=[" + data + "]");
            return UdpReceiveStatus::DatagramReceived;
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
            return UdpReceiveStatus::DatagramReceived;
        }

        Message msg{};
        msg.type = MessageType::PacketRx;
        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = std::move(packet);

        bus_.publish(std::move(msg));

        return UdpReceiveStatus::DatagramReceived;
    }

    int UdpReceiver::fd() const noexcept
    {
        return socket_fd_.get();
    }

    std::uint32_t UdpReceiver::receiveBufferBytes() const noexcept
    {
        return receive_buffer_bytes_;
    }

    void UdpReceiver::processReadableEvent()
    {
        constexpr std::size_t MaxPacketsPerWakeup = 256;

        std::size_t packets_processed = 0;

        while (packets_processed < MaxPacketsPerWakeup)
        {
            auto result = handleReadable();

            if (result == UdpReceiveStatus::DatagramReceived)
            {
                ++packets_processed;
            }
            if (result == UdpReceiveStatus::NoDataAvailable)
            {
                Logger::debug("[UDP] drained queue packets=" + std::to_string(packets_processed));
                break;
            }

            if (result == UdpReceiveStatus::Interrupted)

            {

                continue;
            }

            if (result == UdpReceiveStatus::Closed)
            {
                break;
            }

            if (result == UdpReceiveStatus::Error)
            {
                break;
            }
        }
        if (packets_processed == MaxPacketsPerWakeup)
        {
            Logger::debug("[UDP] receive budget exhausted");
        }
    }

    std::uint32_t UdpReceiver::switchPort() const noexcept
    {
        return switchPort_;
    }

    std::uint16_t UdpReceiver::listenPort() const noexcept
    {
        return listenPort_;
    }
} // namespace edgenetswitch
