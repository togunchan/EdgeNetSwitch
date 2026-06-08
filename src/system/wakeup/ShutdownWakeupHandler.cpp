#include "edgenetswitch/system/wakeup/ShutdownWakeupHandler.hpp"
#include "edgenetswitch/system/event_source/EventFd.hpp"

namespace edgenetswitch
{
    ShutdownWakeupHandler::ShutdownWakeupHandler(EventFd &event_fd) : event_fd_(event_fd) {}

    void ShutdownWakeupHandler::onEvent(const EpollEvent &event)
    {
        auto drained = event_fd_.drain();
    }
} // namespace edgenetswitch