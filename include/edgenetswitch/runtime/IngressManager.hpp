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
    class IngressManager
    {
    public:
        explicit IngressManager(MessagingBus &bus, EpollManager &epollManager,
                                EpollEventLoop &epollEventLoop, FdRegistry &fdRegistry);

        void initialize(const core::UdpConfig &udpConfig);
        void shutdown();

    private:
        MessagingBus &bus_;
        EpollManager &epollManager_;
        EpollEventLoop &epollLoop_;
        FdRegistry &fdRegistry_;

        std::unique_ptr<UdpReceiver> udpReceiver_;
        std::unique_ptr<UdpReadyHandler> udpHandler_;
    };
} // namespace edgenetswitch
