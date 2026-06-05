#pragma once

#include "edgenetswitch/system/FileDescriptor.hpp"

namespace edgenetswitch
{
    class FdRegistry;

    class EpollManager
    {
    public:
        EpollManager(FdRegistry *registry);

        int fd() const noexcept;

        bool valid() const noexcept;

    private:
        FileDescriptor epoll_fd_;
    };
} // namespace edgenetswitch
