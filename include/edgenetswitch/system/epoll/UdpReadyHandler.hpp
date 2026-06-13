#pragma once

#include "edgenetswitch/network/UdpReceiver.hpp"
#include "edgenetswitch/system/epoll/IEpollHandler.hpp"

namespace edgenetswitch
{
    class UdpReceiver;

    class UdpReadyHandler : public IEpollHandler
    {
    public:
        explicit UdpReadyHandler(UdpReceiver &receiver);

        void onEvent(const EpollEvent &event) override;

    private:
        UdpReceiver &receiver_;
    };
} // namespace edgenetswitch