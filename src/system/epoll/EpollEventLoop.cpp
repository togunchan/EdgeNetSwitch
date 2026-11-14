#include "edgenetswitch/system/epoll/EpollEventLoop.hpp"
#include "edgenetswitch/system/epoll/EpollManager.hpp"
#include "edgenetswitch/system/wakeup/ShutdownWakeupHandler.hpp"
#include <atomic>
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

        while (!stop_requested_.load(std::memory_order_acquire))
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
        const bool already_requested = stop_requested_.exchange(true, std::memory_order_acq_rel);

        if (!already_requested)
        {
            shutdown_event_.notify();
        }
    }

    void EpollEventLoop::registerHandler(int fd, IEpollHandler *handler)
    {
        handlers_[fd] = handler;
    }
} // namespace edgenetswitch