#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/failure/FailureInjector.hpp"
#include <condition_variable>
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
        explicit PacketProcessor(MessagingBus &bus, failure::FailureInjector injector);
        ~PacketProcessor();
        void processLoop();
        void processPacket(Packet processedPacket);
        void handleInjectedFailure(const Packet &pkt, const failure::FailureResult &failure, std::uint64_t now_ms);

    private:
        std::deque<Packet> queue_;
        std::mutex queue_mutex_;
        std::condition_variable cv_;
        std::thread worker_;
        std::atomic<bool> running_{true};
        static constexpr size_t MAX_QUEUE_SIZE = 1024;
        static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;
        MessagingBus &bus_;
        failure::FailureInjector injector_;
    };
}
