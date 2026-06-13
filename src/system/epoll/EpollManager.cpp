#include "edgenetswitch/system/epoll/EpollManager.hpp"

#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"
#include "edgenetswitch/system/epoll/EpollEvent.hpp"

#include <cerrno>
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

    EpollManager::~EpollManager() = default;

    int EpollManager::fd() const noexcept
    {
        return epoll_fd_.get();
    }

    bool EpollManager::valid() const noexcept
    {
        return epoll_fd_.valid();
    }

    void EpollManager::add(int fd, std::uint32_t events)
    {
        epoll_event event{};
        event.events = events;
        event.data.fd = fd;

        if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) < 0)
        {
            throw std::runtime_error("epoll add failed");
        }
    }

    void EpollManager::remove(int fd)
    {
        if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) < 0)
        {
            throw std::runtime_error("epoll remove failed");
        }
    }

    std::vector<EpollEvent> EpollManager::wait(int timeout_ms)
    {
        constexpr int MaxEvents = 64;

        epoll_event native_events[MaxEvents]{};

        int ready_count = 0;

        while (true) {
            ready_count = ::epoll_wait(epoll_fd_.get(), native_events, MaxEvents, timeout_ms);

            if (ready_count >= 0)
            {
                break;
            }

            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error("epoll wait failed");
        }

        std::vector<EpollEvent> events;
        events.reserve(ready_count);

        for (int i = 0; i < ready_count; ++i)
        {
            EpollEvent event;
            event.fd = native_events[i].data.fd;
            event.events = native_events[i].events;

            events.push_back(event);
        }

        return events;
    }

} // namespace edgenetswitch
