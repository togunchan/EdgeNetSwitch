#pragma once

#include "edgenetswitch/system/FileDescriptor.hpp"
#include "system/EpollEvent.hpp"
#include <cstdint>
#include <vector>

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

        [[nodiscard]]
        std::vector<EpollEvent> wait(int timeout_ms);

    private:
        FileDescriptor epoll_fd_;
    };
} // namespace edgenetswitch
