#pragma once

#include "edgenetswitch/system/fd/FdType.hpp"
namespace edgenetswitch
{
    class FdRegistry;

    class FileDescriptor
    {
    public:
        FileDescriptor() noexcept = default;
        FileDescriptor(int fd) noexcept;
        FileDescriptor(int fd, FdRegistry *registry, FdType fdType) noexcept;
        ~FileDescriptor();

        // Prevent copying: multiple objects must not own the same FD.
        FileDescriptor(const FileDescriptor &) = delete;

        // Prevent assignment copying: avoids FD leaks and double-close bugs.
        FileDescriptor &operator=(const FileDescriptor &) = delete;

        FileDescriptor(FileDescriptor &&other) noexcept;
        FileDescriptor &operator=(FileDescriptor &&other) noexcept;

        [[nodiscard]] int get() const noexcept;
        [[nodiscard]] bool valid() const noexcept;

        int release() noexcept;
        void reset(int fd = -1, FdType type = FdType::Unknown) noexcept;

    private:
        int fd_{-1};
        FdRegistry *registry_{nullptr};
        FdType fd_type_{FdType::Unknown};
    };
} // namespace edgenetswitch