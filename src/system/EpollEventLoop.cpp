#include "edgenetswitch/system/EpollEventLoop.hpp"
#include "edgenetswitch/system/EpollManager.hpp"

namespace edgenetswitch
{
    EpollEventLoop::EpollEventLoop(EpollManager &epoll) : epoll_(epoll) {}

    void EpollEventLoop::run()
    {
        running_ = true;

        while (running_)
        {
            const auto events = epoll_.wait(1000);
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