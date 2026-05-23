#pragma once

#include <atomic>
#include <thread>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/LifecycleIdGenerator.hpp"
#include "edgenetswitch/system/FdRegistry.hpp"
#include "edgenetswitch/system/FileDescriptor.hpp"

namespace edgenetswitch
{
    class UdpReceiver
    {
    public:
        UdpReceiver(MessagingBus &bus, int port, FdRegistry *fd_registry);
        ~UdpReceiver();

        void start();
        void stop();

    private:
        void run();

        MessagingBus &bus_;
        int port_;
        FileDescriptor socket_fd_;
        FdRegistry *fd_registry_{nullptr};
        std::atomic_bool running_{false};
        std::thread worker_;
        LifecycleIdGenerator lifecycle_gen_;
    };
} // namespace edgenetswitch
