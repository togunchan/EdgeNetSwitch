#include "edgenetswitch/system/fd/FileDescriptor.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FdState.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"

#include <unistd.h>

namespace edgenetswitch
{
    FileDescriptor::FileDescriptor(int fd) noexcept : fd_(fd) {}

    FileDescriptor::FileDescriptor(int fd, FdRegistry *registry, FdType fdType) noexcept
        : fd_(fd), registry_(registry), fd_type_(fdType)
    {
        if (registry_ && valid())
            registry_->registerFd(fd, FdState::Active, fdType);
    }

    FileDescriptor::~FileDescriptor()
    {
        reset();
    }

    FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept
        : fd_(other.fd_), registry_(other.registry_), fd_type_(other.fd_type_)
    {
        other.fd_ = -1;
        other.registry_ = nullptr;
    }

    FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept
    {
        if (this != &other)
        {
            reset();

            fd_ = other.fd_;
            registry_ = other.registry_;

            other.fd_ = -1;
            other.registry_ = nullptr;
        }

        return *this;
    }

    int FileDescriptor::get() const noexcept
    {
        return fd_;
    }

    bool FileDescriptor::valid() const noexcept
    {
        return fd_ >= 0;
    }

    // Releases FD ownership without closing the descriptor.
    int FileDescriptor::release() noexcept
    {
        const int old_fd = fd_;

        if (registry_ && old_fd >= 0)
        {
            registry_->updateState(old_fd, FdState::Released);
        }

        fd_ = -1;

        return old_fd;
    }

    void FileDescriptor::reset(int fd, FdType type) noexcept
    {
        if (fd_ >= 0)
        {
            if (registry_)
            {
                registry_->updateState(fd_, FdState::Closed);
                registry_->unregisterFd(fd_);
            }

            ::close(fd_);
        }
        fd_ = fd;

        if (registry_ && fd_ >= 0)
        {
            registry_->registerFd(fd_, FdState::Active, type);
        }
    }

} // namespace edgenetswitch
