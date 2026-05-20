#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/system/FileDescriptor.hpp"

#include <cerrno>
#include <fcntl.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

using namespace edgenetswitch;

static_assert(!std::is_copy_constructible_v<FileDescriptor>);
static_assert(!std::is_copy_assignable_v<FileDescriptor>);
static_assert(std::is_nothrow_move_constructible_v<FileDescriptor>);
static_assert(std::is_nothrow_move_assignable_v<FileDescriptor>);

namespace
{
    int openTestDescriptor()
    {
        const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        REQUIRE(fd >= 0);
        return fd;
    }

    bool isClosed(int fd)
    {
        errno = 0;
        return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
    }
} // namespace

TEST_CASE("FileDescriptor is invalid by default", "[FileDescriptor]")
{
    const FileDescriptor descriptor;

    REQUIRE_FALSE(descriptor.valid());
    REQUIRE(descriptor.get() == -1);
}

TEST_CASE("FileDescriptor reports valid owned descriptors", "[FileDescriptor]")
{
    FileDescriptor descriptor(openTestDescriptor());
    const int fd = descriptor.get();

    REQUIRE(descriptor.valid());
    REQUIRE(descriptor.get() == fd);

    REQUIRE(descriptor.release() == fd);
    REQUIRE(::close(fd) == 0);
}

TEST_CASE("FileDescriptor move construction transfers ownership", "[FileDescriptor]")
{
    FileDescriptor source(openTestDescriptor());
    const int fd = source.get();

    FileDescriptor target(std::move(source));

    REQUIRE_FALSE(source.valid());
    REQUIRE(source.get() == -1);
    REQUIRE(target.valid());
    REQUIRE(target.get() == fd);

    REQUIRE(target.release() == fd);
    REQUIRE(::close(fd) == 0);
}

TEST_CASE("FileDescriptor move assignment transfers ownership", "[FileDescriptor]")
{
    FileDescriptor source(openTestDescriptor());
    FileDescriptor target(openTestDescriptor());

    const int source_fd = source.get();
    const int previous_target_fd = target.get();

    target = std::move(source);

    REQUIRE_FALSE(source.valid());
    REQUIRE(source.get() == -1);
    REQUIRE(target.valid());
    REQUIRE(target.get() == source_fd);
    REQUIRE(isClosed(previous_target_fd));

    REQUIRE(target.release() == source_fd);
    REQUIRE(::close(source_fd) == 0);
}

TEST_CASE("FileDescriptor release transfers ownership without closing", "[FileDescriptor]")
{
    FileDescriptor descriptor(openTestDescriptor());
    const int fd = descriptor.get();

    const int released_fd = descriptor.release();

    REQUIRE(released_fd == fd);
    REQUIRE_FALSE(descriptor.valid());
    REQUIRE(descriptor.get() == -1);
    REQUIRE_FALSE(isClosed(released_fd));

    REQUIRE(::close(released_fd) == 0);
}

TEST_CASE("FileDescriptor destructor closes owned descriptors", "[FileDescriptor]")
{
    int fd = -1;
    {
        FileDescriptor descriptor(openTestDescriptor());
        fd = descriptor.get();
        REQUIRE_FALSE(isClosed(fd));
    }

    REQUIRE(isClosed(fd));
}

TEST_CASE("FileDescriptor reset replaces ownership", "[FileDescriptor]")
{
    FileDescriptor descriptor(openTestDescriptor());
    const int previous_fd = descriptor.get();
    const int replacement_fd = openTestDescriptor();

    descriptor.reset(replacement_fd);

    REQUIRE(isClosed(previous_fd));
    REQUIRE(descriptor.valid());
    REQUIRE(descriptor.get() == replacement_fd);

    REQUIRE(descriptor.release() == replacement_fd);
    REQUIRE(::close(replacement_fd) == 0);
}

TEST_CASE("FileDescriptor self move assignment preserves ownership", "[FileDescriptor]")
{
    FileDescriptor descriptor(openTestDescriptor());
    const int fd = descriptor.get();
    auto &same_descriptor = descriptor;

    descriptor = std::move(same_descriptor);

    REQUIRE(descriptor.valid());
    REQUIRE(descriptor.get() == fd);

    REQUIRE(descriptor.release() == fd);
    REQUIRE(::close(fd) == 0);
}

TEST_CASE("FileDescriptor move chains preserve unique ownership", "[FileDescriptor]")
{
    FileDescriptor first(openTestDescriptor());
    const int owned_fd = first.get();

    FileDescriptor second(std::move(first));
    FileDescriptor third;
    third = std::move(second);
    FileDescriptor fourth(std::move(third));

    REQUIRE_FALSE(first.valid());
    REQUIRE_FALSE(second.valid());
    REQUIRE_FALSE(third.valid());
    REQUIRE(fourth.valid());
    REQUIRE(fourth.get() == owned_fd);

    FileDescriptor fifth(openTestDescriptor());
    const int previous_fifth_fd = fifth.get();

    fifth = std::move(fourth);

    REQUIRE_FALSE(fourth.valid());
    REQUIRE(fifth.valid());
    REQUIRE(fifth.get() == owned_fd);
    REQUIRE(isClosed(previous_fifth_fd));

    REQUIRE(fifth.release() == owned_fd);
    REQUIRE(::close(owned_fd) == 0);
}
