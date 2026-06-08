#pragma once

#include "edgenetswitch/system/epoll/IEpollHandler.hpp"

namespace edgenetswitch
{
    class EventFd;

    class ShutdownWakeupHandler : public IEpollHandler
    {
    public:
        explicit ShutdownWakeupHandler(EventFd& event_fd);

        void onEvent(const EpollEvent& event) override;

    private:
        EventFd& event_fd_;
    };
} // namespace edgenetswitch