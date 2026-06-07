#include "edgenetswitch/system/epoll/EpollEventLoop.hpp"
#include "edgenetswitch/system/epoll/EpollManager.hpp"

namespace edgenetswitch
{
    EpollEventLoop::EpollEventLoop(EpollManager &epoll) : epoll_(epoll) {}

    void EpollEventLoop::run()
    {
        running_ = true;

        while (running_)
        {
            const auto events = epoll_.wait(1000);

            for (const auto& event : events)
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
    }

    void EpollEventLoop::registerHandler(int fd, IEpollHandler *handler)
    {
        handlers_[fd] = handler;
    }
} // namespace edgenetswitch