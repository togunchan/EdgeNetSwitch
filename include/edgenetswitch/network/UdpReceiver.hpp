#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include <atomic>
#include <thread>

namespace edgenetswitch
{
    class PacketStats;

    class UdpReceiver
    {
    public:
        UdpReceiver(MessagingBus &bus, PacketStats &stats, int port);
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
        PacketStats &stats_;
    };
} // namespace edgenetswitch
