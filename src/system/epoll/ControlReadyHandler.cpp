#include "edgenetswitch/system/epoll/ControlReadyHandler.hpp"

#include "edgenetswitch/control/ControlServer.hpp"

namespace edgenetswitch
{
    ControlReadyHandler::ControlReadyHandler(control::ControlServer &server) : server_(server) {}

    void ControlReadyHandler::onEvent(const EpollEvent &)
    {
        server_.processReadableEvent();
    }
} // namespace edgenetswitch