#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/system/EpollManager.hpp"
#include "edgenetswitch/system/EventFd.hpp"
#include "edgenetswitch/system/FdRegistry.hpp"

#include <sys/epoll.h>

using namespace edgenetswitch;

TEST_CASE("EpollManager reports EventFd readiness after notification", "[EpollManager][EventFd]")
{
    FdRegistry registry;
    EventFd eventfd(&registry);
    EpollManager epoll(&registry);

    epoll.add(eventfd.fd(), EPOLLIN);

    eventfd.notify();

    const auto events = epoll.wait(100);

    REQUIRE(events.size() == 1);
    CHECK(events[0].fd == eventfd.fd());
    CHECK((events[0].events & EPOLLIN) != 0);

    CHECK(eventfd.drain() == 1);
}
