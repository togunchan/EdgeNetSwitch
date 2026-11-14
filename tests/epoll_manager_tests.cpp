#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/system/epoll/EpollEventLoop.hpp"
#include "edgenetswitch/system/epoll/EpollManager.hpp"
#include "edgenetswitch/system/event_source/EventFd.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"

#include <algorithm>
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

TEST_CASE("EpollEventLoop honors stop requests made before run", "[EpollEventLoop]")
{
    struct UnexpectedEventHandler final : IEpollHandler
    {
        void onEvent(const EpollEvent &) override
        {
            FAIL("The stopped event loop dispatched a ready event");
        }
    };

    FdRegistry registry;
    EventFd eventfd(&registry);
    EpollManager epoll(&registry);
    UnexpectedEventHandler handler;
    EpollEventLoop loop(epoll, &registry);

    epoll.add(eventfd.fd(), EPOLLIN | EPOLLONESHOT);
    loop.registerHandler(eventfd.fd(), &handler);
    eventfd.notify();

    loop.stop();
    SECTION("one stop request")
    {
        loop.run();
    }
    SECTION("repeated stop requests")
    {
        loop.stop();
        loop.run();
    }

    // A one-shot event is disabled once epoll delivers it.
    const auto pending_events = epoll.wait(0);
    CHECK(std::ranges::any_of(pending_events,
                              [&](const EpollEvent &event) { return event.fd == eventfd.fd(); }));
}
