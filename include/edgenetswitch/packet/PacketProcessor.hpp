#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include <thread>
#include <deque>
#include <mutex>
#include <atomic>

namespace edgenetswitch
{

    class PacketProcessor
    {
    public:
        explicit PacketProcessor(MessagingBus &bus);
        ~PacketProcessor();
        void processLoop();
        void processPacket(Packet processedPacket);

    private:
        std::deque<Packet> queue_;
        std::mutex queue_mutex_;
        std::thread worker_;
        std::atomic<bool> running_{true};
        static constexpr size_t MAX_QUEUE_SIZE = 1024;
        static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;
        MessagingBus &bus_;
    };

}
