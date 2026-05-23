#pragma once

namespace edgenetswitch
{
    enum class FdType
    {
        Unknown,
        UdpSocket,
        UnixSocket,
        Epoll,
        Pipe,
        EventFd
    };
} // namespace edgenetswitch