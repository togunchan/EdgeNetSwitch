#pragma once

#include "edgenetswitch/system/EpollManager.hpp"
#include "edgenetswitch/system/IEpollHandler.hpp"
#include <map>
namespace edgenetswitch
{
    class EpollManager;

    class EpollEventLoop
    {
    public:
        explicit EpollEventLoop(EpollManager &epoll);

        void run();
        void stop();
        void registerHandler(int fd, IEpollHandler *handler);

    private:
        EpollManager &epoll_;
        bool running_{false};
        std::map<int, IEpollHandler *> handlers_;
    };
} // namespace edgenetswitch