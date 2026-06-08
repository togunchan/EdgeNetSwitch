#include "edgenetswitch/system/event_source/EventFd.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"
#include "edgenetswitch/system/fd/FileDescriptor.hpp"

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <sys/eventfd.h>
#include <unistd.h>

namespace edgenetswitch
{
    namespace
    {
        int createEventFd()
        {
            const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

            if (fd < 0)
            {
                throw std::runtime_error("eventfd creation failed");
            }

            return fd;
        }
    } // namespace

    EventFd::EventFd(FdRegistry *registry) : fd_(createEventFd(), registry, FdType::EventFd) {}

    int EventFd::fd() const noexcept
    {
        return fd_.get();
    }

    bool EventFd::valid() const noexcept
    {
        return fd_.valid();
    }

    void EventFd::notify()
    {
        const std::uint64_t value = 1;

        while (true)
        {
            const ssize_t written = ::write(fd_.get(), &value, sizeof(value));

            if (written == static_cast<ssize_t>(sizeof(value)))
            {
                // The wakeup notification was written successfully.
                return;
            }

            if (written < 0 && errno == EINTR)
            {
                // Retry if the write was interrupted by a signal.
                continue;
            }

            throw std::runtime_error("eventfd notify failed");
        }
    }

    std::uint64_t EventFd::drain()
    {
        std::uint64_t value = 0;

        while (true)
        {
            const ssize_t bytes_read = ::read(fd_.get(), &value, sizeof(value));

            if (bytes_read == static_cast<ssize_t>(sizeof(value)))
            {
                // Successfully drained the accumulated wakeup counter.
                return value;
            }

            if (bytes_read < 0 && errno == EINTR)
            {
                // Retry if the read was interrupted by a signal.
                continue;
            }

            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                // Nothing to drain right now.
                return 0;
            }

            throw std::runtime_error("eventfd drain failed");
        }
    }

} // namespace edgenetswitch