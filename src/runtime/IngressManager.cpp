#include "edgenetswitch/runtime/IngressManager.hpp"
#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/network/IngressMode.hpp"
#include "edgenetswitch/network/UdpReceiver.hpp"
#include <memory>
#include <sys/epoll.h>

namespace edgenetswitch
{
    IngressManager::IngressManager(MessagingBus &bus, EpollManager &epollManager,
                                   EpollEventLoop &epollEventLoop, FdRegistry &fdRegistry)
        : bus_(bus), epollManager_(epollManager), epollLoop_(epollEventLoop),
          fdRegistry_(fdRegistry)
    {
    }

    void IngressManager::initialize(const core::UdpConfig &udpConfig)
    {
        udpReceiver_ = std::make_unique<UdpReceiver>(bus_, udpConfig.port, &fdRegistry_,
                                                     IngressMode::NonBlocking);
        udpReceiver_->initializeSocket();

        udpHandler_ = std::make_unique<UdpReadyHandler>(*udpReceiver_);
        Logger::debug("UDP fd = " + std::to_string(udpReceiver_->fd()));

        epollManager_.add(udpReceiver_->fd(), EPOLLIN);
        epollLoop_.registerHandler(udpReceiver_->fd(), udpHandler_.get());
    }
} // namespace edgenetswitch