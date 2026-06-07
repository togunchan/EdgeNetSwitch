#pragma once

#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FileDescriptor.hpp"

#include <cstdint>

namespace edgenetswitch
{
    class EventFd
    {
    public:
        explicit EventFd(FdRegistry *registry = nullptr);

        EventFd(const EventFd &) = delete;
        EventFd &operator=(const EventFd &) = delete;

        EventFd(EventFd &&) noexcept = default;
        EventFd &operator=(EventFd &&) noexcept = default;

        [[nodiscard]]
        int fd() const noexcept;

        [[nodiscard]]
        bool valid() const noexcept;

        void notify();

        [[nodiscard]]
        std::uint64_t drain();

    private:
        FileDescriptor fd_;
    };
} // namespace edgenetswitch