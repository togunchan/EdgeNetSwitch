#pragma once

#include <atomic>
#include <thread>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/network/IngressMode.hpp"
#include "edgenetswitch/packet/LifecycleIdGenerator.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FileDescriptor.hpp"

namespace edgenetswitch
{
    enum class UdpReceiveStatus
    {
        DatagramReceived,
        NoDataAvailable,
        Interrupted,
        Closed,
        Error
    };

    class UdpReceiver
    {
    public:
        UdpReceiver(MessagingBus &bus, std::uint32_t switchPort, std::uint16_t listenPort,
                    LifecycleIdGenerator &lifecycle_gen, FdRegistry *fd_registry,
                    IngressMode ingress_mode = IngressMode::Blocking);
        ~UdpReceiver();

        void initializeSocket();
        void start();
        void stop();

        void processReadableEvent();

        [[nodiscard]] int fd() const noexcept;
        [[nodiscard]] std::uint32_t receiveBufferBytes() const noexcept;
        [[nodiscard]] std::uint32_t switchPort() const noexcept;
        [[nodiscard]] std::uint16_t listenPort() const noexcept;

    private:
        void run();
        UdpReceiveStatus handleReadable();

        MessagingBus &bus_;
        std::uint32_t switchPort_;
        std::uint16_t listenPort_;
        FileDescriptor socket_fd_;
        FdRegistry *fd_registry_{nullptr};
        std::atomic_bool running_{false};
        std::thread worker_;
        LifecycleIdGenerator &lifecycle_gen_;
        IngressMode ingress_mode_{IngressMode::Blocking};
        std::uint32_t receive_buffer_bytes_{0};
    };
} // namespace edgenetswitch
