#include "edgenetswitch/system/EpollManager.hpp"

#include "edgenetswitch/system/FdRegistry.hpp"
#include "edgenetswitch/system/FdType.hpp"

#include <stdexcept>
#include <sys/epoll.h>

namespace edgenetswitch
{
    namespace
    {
        int createEpollFd()
        {
            const int fd = ::epoll_create1(EPOLL_CLOEXEC);

            if (fd < 0)
            {
                throw std::runtime_error("epoll_create1 failed");
            }

            return fd;
        }
    } // namespace

    EpollManager::EpollManager(FdRegistry *registry)
        : epoll_fd_(createEpollFd(), registry, FdType::Epoll)
    {
    }

    int EpollManager::fd() const noexcept
    {
        return epoll_fd_.get();
    }

    bool EpollManager::valid() const noexcept
    {
        return epoll_fd_.valid();
    }

} // namespace edgenetswitch