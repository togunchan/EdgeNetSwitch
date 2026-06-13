#pragma once

#include "edgenetswitch/system/epoll/EpollManager.hpp"
#include "edgenetswitch/system/epoll/IEpollHandler.hpp"
#include "edgenetswitch/system/event_source/EventFd.hpp"
#include "edgenetswitch/system/wakeup/ShutdownWakeupHandler.hpp"
#include <atomic>
#include <map>
namespace edgenetswitch
{
    class EpollManager;
    class FdRegistry;

    class EpollEventLoop
    {
    public:
        explicit EpollEventLoop(EpollManager &epoll, FdRegistry *registry);

        void run();
        void stop();
        void registerHandler(int fd, IEpollHandler *handler);

    private:
        EpollManager &epoll_;
        std::atomic<bool> running_{false};
        std::map<int, IEpollHandler *> handlers_;
        EventFd shutdown_event_;
        ShutdownWakeupHandler shutdown_handler_;
    };
} // namespace edgenetswitch