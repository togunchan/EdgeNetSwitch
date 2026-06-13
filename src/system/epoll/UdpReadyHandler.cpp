#include "edgenetswitch/system/epoll/UdpReadyHandler.hpp"
#include "edgenetswitch/system/epoll/EpollEvent.hpp"
#include "edgenetswitch/core/Logger.hpp"

namespace edgenetswitch
{
    UdpReadyHandler::UdpReadyHandler(UdpReceiver &receiver) : receiver_(receiver) {}

    void UdpReadyHandler::onEvent(const EpollEvent &)
    {
        Logger::debug("[EPOLL] UDP readable");
        receiver_.processReadableEvent();
    }
} // namespace edgenetswitch
