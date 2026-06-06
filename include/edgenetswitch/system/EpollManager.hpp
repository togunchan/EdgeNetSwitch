#pragma once

#include "edgenetswitch/system/FileDescriptor.hpp"
#include <cstdint>

namespace edgenetswitch
{
    class FdRegistry;

    class EpollManager
    {
    public:
        EpollManager(FdRegistry *registry);

        [[nodiscard]]
        int fd() const noexcept;

        [[nodiscard]]
        bool valid() const noexcept;

        void add(int fd, std::uint32_t events);

        void remove(int fd);

    private:
        FileDescriptor epoll_fd_;
    };
} // namespace edgenetswitch
