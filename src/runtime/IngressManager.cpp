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
        for (const auto &endpointConfig : udpConfig.endpoints)
        {
            UdpIngressEndpoint endpoint;

            endpoint.receiver = std::make_unique<UdpReceiver>(
                bus_, endpointConfig.switch_port, endpointConfig.listen_port, &fdRegistry_,
                IngressMode::NonBlocking);
            endpoint.receiver->initializeSocket();

            endpoint.handler = std::make_unique<UdpReadyHandler>(*endpoint.receiver);
            Logger::debug("UDP fd = " + std::to_string(endpoint.receiver->fd()));

            epollManager_.add(endpoint.receiver->fd(), EPOLLIN);
            epollLoop_.registerHandler(endpoint.receiver->fd(), endpoint.handler.get());

            ingressEndpoints_.push_back(std::move(endpoint));
        }
    }

    void IngressManager::shutdown()
    {
        for (auto &endpoint : ingressEndpoints_)
        {
            Logger::info("[SHUTDOWN] Stopping UDP receiver");
            endpoint.receiver->stop();
        }
        Logger::info("[SHUTDOWN] UDP receiver stopped");
        ingressEndpoints_.clear();
    }

} // namespace edgenetswitch