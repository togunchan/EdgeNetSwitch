#pragma once

#include "edgenetswitch/system/epoll/IEpollHandler.hpp"

namespace edgenetswitch::control
{
    class ControlServer;
}

namespace edgenetswitch
{
    class ControlReadyHandler : public IEpollHandler
    {
    public:
        explicit ControlReadyHandler(control::ControlServer &server);

        void onEvent(const EpollEvent &event) override;

    private:
        control::ControlServer &server_;
    };
} // namespace edgenetswitch