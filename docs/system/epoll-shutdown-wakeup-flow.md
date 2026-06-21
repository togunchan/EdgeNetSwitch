# v1.9.3 — Epoll Shutdown Wakeup Flow

## Problem Statement

The v1.9.3 shutdown wakeup path exists because an epoll-based event loop can be
blocked inside `epoll_wait()` when shutdown is requested.

The loop state is controlled by `running_`:

```cpp
running_ = true;

while (running_)
{
    const auto events = epoll_.wait(1000);

    for (const auto& event : events)
    {
        dispatch(event);
    }
}
```

Setting `running_ = false` is necessary, but it is not sufficient by itself.
`running_` is ordinary user-space state. The kernel does not monitor that
boolean while the thread is blocked inside `epoll_wait()`.

`epoll_wait()` returns only when:

- a registered file descriptor becomes ready
- the timeout expires
- the wait is interrupted by a signal
- an error occurs

In the current implementation the wait uses a finite timeout, so the loop would
eventually observe `running_ = false` after the timeout expires. That still
makes shutdown depend on timeout expiry. If the timeout becomes longer or
unbounded, shutdown would depend on unrelated descriptor readiness unless the
loop has an explicit wakeup source.

The shutdown path therefore needs two pieces:

```text
running_ = false
    records that the loop should exit

shutdown_event_.notify()
    creates kernel-visible fd readiness so epoll_wait() returns
```

In v1.9.4, signal-aware shutdown reason capture happens before this epoll
wakeup path. `SIGINT` and `SIGTERM` are recorded by the daemon signal handler
and converted into a `ShutdownRequest` by the main loop. The signal reason
model explains why shutdown was requested; the `eventfd` wakeup path explains
how the blocked epoll thread is made runnable so it can stop.

## Architecture Overview

The shutdown wakeup design uses a Linux `eventfd` as an epoll-visible stop
signal. `EventFd` is the reusable low-level event source. `EpollManager`
monitors file descriptors. `EpollEventLoop` owns the shutdown event source and
dispatch table. `ShutdownWakeupHandler` consumes the shutdown wakeup.

### Ownership Model

```text
EpollManager
  owns:
    epoll fd

EpollEventLoop
  references:
    EpollManager

  owns:
    running_
    handlers_
    shutdown_event_          EventFd
    shutdown_handler_        ShutdownWakeupHandler

ShutdownWakeupHandler
  owns:
    no file descriptor

  references:
    EventFd owned by EpollEventLoop
```

Ownership is about lifetime. `EpollManager` owns the epoll instance.
`EpollEventLoop` owns the shutdown `EventFd` and the handler object that drains
it. `ShutdownWakeupHandler` only holds a reference to the loop-owned `EventFd`.

Registering a descriptor with epoll does not transfer ownership of that
descriptor to epoll. The object that created the descriptor remains responsible
for its lifetime.

### Wiring Model

```text
EpollEventLoop constructor
        |
        +-- epoll_.add(shutdown_event_.fd(), EPOLLIN)
        |        |
        |        v
        |   EpollManager watches the EventFd file descriptor
        |
        +-- registerHandler(shutdown_event_.fd(), &shutdown_handler_)
                 |
                 v
            handlers_[eventfd fd] = ShutdownWakeupHandler
```

The shutdown `EventFd` exposes an integer file descriptor. `EpollEventLoop`
registers that descriptor with `EpollManager` so `epoll_wait()` can report it
when readable. The same descriptor is stored in `handlers_` so returned
`EpollEvent` values can be dispatched to `ShutdownWakeupHandler`.

## Shutdown Flow

The implemented shutdown path is:

```text
stop()
  |
  v
running_ = false
  |
  v
shutdown_event_.notify()
  |
  v
eventfd becomes readable
  |
  v
epoll_wait() returns
  |
  v
EpollEventLoop receives event
  |
  v
handlers_[eventfd fd] selects ShutdownWakeupHandler
  |
  v
ShutdownWakeupHandler executes
  |
  v
event_fd_.drain()
  |
  v
while(running_) evaluates false
  |
  v
event loop exits
```

`stop()` first sets `running_ = false`, recording the desired loop state. It
then calls `shutdown_event_.notify()`, which writes `1` to the eventfd counter.
That makes the eventfd readable.

Because the eventfd was registered with epoll for `EPOLLIN`, `epoll_wait()`
returns an event containing the shutdown descriptor. `EpollEventLoop` looks up
that descriptor in `handlers_` and invokes `ShutdownWakeupHandler::onEvent()`.

The handler drains the eventfd counter. It does not decide whether the loop
should stop; that decision was already recorded by `running_ = false`. After
dispatch completes, control reaches the next `while(running_)` check, evaluates
false, and exits the loop.

## Component Responsibilities

### EventFd

`EventFd` is the low-level Linux event source. It owns a descriptor created with
`eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)` and exposes:

- `fd()` for epoll registration
- `notify()` to make the descriptor readable
- `drain()` to consume readiness and clear the counter

`EventFd` does not know about shutdown. It is a reusable event-source primitive.

### EpollManager

`EpollManager` owns the epoll instance and wraps:

- `add(fd, events)`
- `remove(fd)`
- `wait(timeout_ms)`

It monitors the shutdown descriptor but does not own it. `wait()` calls
`epoll_wait()` and converts native epoll results into `EpollEvent` values.

### EpollEventLoop

`EpollEventLoop` coordinates the shutdown path. During construction it registers
the shutdown eventfd with epoll and maps the descriptor to
`ShutdownWakeupHandler`:

```cpp
epoll_.add(shutdown_event_.fd(), EPOLLIN);
registerHandler(shutdown_event_.fd(), &shutdown_handler_);
```

During shutdown it records the desired stop state and wakes the wait:

```cpp
running_ = false;
shutdown_event_.notify();
```

### ShutdownWakeupHandler

`ShutdownWakeupHandler` is named after behavior, not the Linux primitive. The
event source happens to be an `EventFd`, but the runtime meaning is shutdown
wakeup.

Its responsibility is intentionally narrow:

```cpp
event_fd_.drain();
```

The handler owns no descriptor. It drains the loop-owned `EventFd` after epoll
reports it as readable.

## `notify()` vs `drain()`

`notify()` and `drain()` are the producer and consumer sides of the eventfd
protocol.

```text
producer side

stop()
  |
  v
notify()
  |
  v
write 1 to eventfd
  |
  v
counter increments
  |
  v
fd becomes readable
```

```text
consumer side

epoll event
  |
  v
ShutdownWakeupHandler
  |
  v
drain()
  |
  v
read eventfd counter
  |
  v
fd is no longer readable
```

Without `notify()`, the event loop may remain blocked in `epoll_wait()`.
Without `drain()`, the eventfd remains readable after the notification has been
observed, leaving stale readiness in the epoll set.

The eventfd counter also coalesces notifications. If multiple writes happen
before the loop drains the descriptor, one read observes the accumulated counter
value and clears the readable state.

## Future Extensibility

The shutdown wakeup path establishes the general epoll-source pattern:

```text
source object owns fd
        |
        v
EpollManager registers fd
        |
        v
EpollEventLoop maps fd to behavior-specific handler
        |
        v
handler consumes readiness and performs bounded work
```

Future sources can reuse the same structure without changing the shutdown path:

- UDP handlers can register nonblocking sockets and process `EPOLLIN` when
  datagrams are ready.
- `TimerFd` handlers can register timer descriptors for periodic runtime work
  without sleeping inside the event loop.
- `SignalFd` handlers can translate selected Linux signals into ordinary fd
  readiness events.

Handlers should continue to be named after behavior rather than the primitive.
A future UDP handler should describe UDP readiness, a timer handler should
describe the timer behavior it owns, and a signal handler should describe the
signal flow it represents.

The shutdown path remains the smallest useful example of the model: one source,
one readiness event, one behavior-specific handler, and one loop-exit decision.
