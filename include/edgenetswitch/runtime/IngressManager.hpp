#pragma once

#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/network/UdpReceiver.hpp"
#include "edgenetswitch/system/epoll/EpollEventLoop.hpp"
#include "edgenetswitch/system/epoll/EpollManager.hpp"
#include "edgenetswitch/system/epoll/UdpReadyHandler.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include <memory>

namespace edgenetswitch
{
    struct IngressSocketSnapshot
    {
        std::uint32_t switch_port{0};
        std::uint16_t listen_port{0};
        int fd{-1};
        std::uint32_t receive_buffer_bytes{0};
    };

    class IngressManager
    {
    public:
        explicit IngressManager(MessagingBus &bus, LifecycleIdGenerator &lifecycleGenerator,
                                EpollManager &epollManager, EpollEventLoop &epollEventLoop,
                                FdRegistry &fdRegistry);

        void initialize(const core::UdpConfig &udpConfig);
        void shutdown();

        [[nodiscard]] std::vector<IngressSocketSnapshot> snapshot() const;

    private:
        MessagingBus &bus_;
        EpollManager &epollManager_;
        EpollEventLoop &epollLoop_;
        FdRegistry &fdRegistry_;

        struct UdpIngressEndpoint
        {
            std::unique_ptr<UdpReceiver> receiver;
            std::unique_ptr<UdpReadyHandler> handler;
        };

        std::vector<UdpIngressEndpoint> ingressEndpoints_;
        LifecycleIdGenerator &lifecycleGenerator_;
    };
} // namespace edgenetswitch
