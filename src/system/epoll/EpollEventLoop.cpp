#include "edgenetswitch/system/epoll/EpollEventLoop.hpp"
#include "edgenetswitch/system/epoll/EpollManager.hpp"
#include "edgenetswitch/system/wakeup/ShutdownWakeupHandler.hpp"
#include <sys/epoll.h>

namespace edgenetswitch
{
    EpollEventLoop::EpollEventLoop(EpollManager &epoll, FdRegistry *registry)
        : epoll_(epoll), shutdown_event_(registry), shutdown_handler_(shutdown_event_)
    {
        epoll_.add(shutdown_event_.fd(), EPOLLIN);
        registerHandler(shutdown_event_.fd(), &shutdown_handler_);
    }

    void EpollEventLoop::run()
    {
        running_ = true;

        while (running_)
        {
            const auto events = epoll_.wait(1000);

            for (const auto &event : events)
            {
                auto it = handlers_.find(event.fd);
                if (it == handlers_.end())
                {
                    // Ignore events for unregistered file descriptors.
                    continue;
                }

                it->second->onEvent(event);
            }
        }
    }

    void EpollEventLoop::stop()
    {
        running_ = false;
        shutdown_event_.notify();
    }

    void EpollEventLoop::registerHandler(int fd, IEpollHandler *handler)
    {
        handlers_[fd] = handler;
    }
} // namespace edgenetswitch