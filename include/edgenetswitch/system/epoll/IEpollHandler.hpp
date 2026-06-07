#pragma once

#include "edgenetswitch/system/epoll/EpollEvent.hpp"

namespace edgenetswitch
{
    class IEpollHandler
    {
    public:
        virtual ~IEpollHandler() = default;

        virtual void onEvent(const EpollEvent &event) = 0;
    };
} // namespace edgenetswitch