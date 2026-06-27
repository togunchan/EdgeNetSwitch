#pragma once

#include "edgenetswitch/failure/FailureInjector.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/transport/TransportManager.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

namespace edgenetswitch
{
    class PacketProcessor
    {
    public:
        explicit PacketProcessor(
            MessagingBus &bus,
            SwitchForwardingEngine *forwarding_engine = nullptr,
            transport::TransportManager *transport_manager = nullptr,
            failure::FailureInjector injector =
                failure::FailureInjector{failure::FailureConfig{}});
        ~PacketProcessor();
        void processLoop();
        void processPacket(Packet processedPacket);
        void handleInjectedFailure(const Packet &pkt, const failure::FailureResult &failure,
                                   std::uint64_t now_ms);

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
        SwitchForwardingEngine *forwarding_engine_{nullptr};
    };
} // namespace edgenetswitch
