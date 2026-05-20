#include "edgenetswitch/system/FileDescriptor.hpp"

#include <unistd.h>

namespace edgenetswitch
{
    FileDescriptor::FileDescriptor(int fd) noexcept : fd_(fd) {}

    FileDescriptor::~FileDescriptor()
    {
        reset();
    }

    FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept : fd_(other.release()) {}

    FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
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

        fd_ = -1;

        return old_fd;
    }

    void FileDescriptor::reset(int fd) noexcept
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
        fd_ = fd;
    }

} // namespace edgenetswitch
