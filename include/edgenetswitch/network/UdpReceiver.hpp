#pragma once

#include <atomic>
#include <thread>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/LifecycleIdGenerator.hpp"

namespace edgenetswitch
{
    class UdpReceiver
    {
    public:
        UdpReceiver(MessagingBus &bus, int port);
        ~UdpReceiver();

        void start();
        void stop();

    private:
        void run();

        MessagingBus &bus_;
        int port_;
        int sockfd_;
        std::atomic_bool running_{false};
        std::thread worker_;
        LifecycleIdGenerator lifecycle_gen_;
    };
} // namespace edgenetswitch
