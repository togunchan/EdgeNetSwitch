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
    enum class UdpReadResult
    {
        PacketProcessed,
        NoData,
        Closed,
        Error
    };

    class UdpReceiver
    {
    public:
        UdpReceiver(MessagingBus &bus, int port, FdRegistry *fd_registry,
                    IngressMode ingress_mode = IngressMode::Blocking);
        ~UdpReceiver();

        void initializeSocket();
        void start();
        void stop();

        [[nodiscard]]
        int fd() const noexcept;

        void processReadableEvent();

    private:
        void run();
        UdpReadResult handleReadable();

        MessagingBus &bus_;
        int port_;
        FileDescriptor socket_fd_;
        FdRegistry *fd_registry_{nullptr};
        std::atomic_bool running_{false};
        std::thread worker_;
        LifecycleIdGenerator lifecycle_gen_;
        IngressMode ingress_mode_{IngressMode::Blocking};
    };
} // namespace edgenetswitch
