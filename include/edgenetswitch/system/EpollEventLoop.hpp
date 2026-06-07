#pragma once

#include "edgenetswitch/system/EpollManager.hpp"
namespace edgenetswitch
{
    class EpollManager;

    class EpollEventLoop
    {
    public:
        explicit EpollEventLoop(EpollManager &epoll);

        void run();
        void stop();

    private:
        EpollManager &epoll_;
        bool running_{false};
    };
} // namespace edgenetswitch