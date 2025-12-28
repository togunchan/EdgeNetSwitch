# EdgeNetSwitch

Virtual embedded Linux edge device platform for deterministic, testable user-space daemons before real hardware, networking, or Yocto integration. Version: v1.1.0.

## Why this exists
- Embedded/networking teams need a safe, repeatable target before boards, NICs, or BSPs exist.
- Focuses on deterministic daemon design with built-in observability so runtime behavior is explainable.
- Keeps architecture explicit and testable, enabling confident iteration before hardware bring-up.

## Scope

### v1.0 – Runtime Core (stabilized)
- Deterministic tick-driven daemon
- Thread-safe Logger (JSON-configured)
- In-process MessagingBus (pub/sub)
- JSON ConfigLoader
- Telemetry (uptime, tick counter)
- HealthMonitor with heartbeat-based transitions
- SystemStart / SystemShutdown lifecycle events
- Full Catch2 unit test coverage

### v1.1 – Control Plane & Observability
- UNIX domain socket control plane at /tmp/edgenetswitch.sock
- Dedicated control thread (non-blocking to runtime)
- Read-only runtime inspection
- CLI command: status
- Live telemetry snapshot retrieval
- Clean separation between runtime, control plane, and CLI
- Graceful shutdown coordination between threads

### Intentionally not included (as of v1.1)
- Runtime mutation or control commands
- Networking / switching logic
- Yocto or QEMU integration
- Remote TCP/IP management
- Authentication or authorization
- Hard real-time guarantees
Deferred to keep the runtime deterministic, avoid premature coupling to hardware/network stacks, and preserve a minimal, observable surface while the control plane stabilizes.

## Runtime & control-plane architecture
The tick-driven runtime owns execution while all subsystems communicate via the in-process MessagingBus. An out-of-band control thread exposes read-only inspection over a UNIX socket without pausing the runtime.

```
+-------------------------------------------------------------+
| Runtime core (tick loop)                                    |
|                                                             |
|  +-----------+     +---------------+     +---------------+  |
|  | Telemetry |-->  | MessagingBus  | <-> | HealthMonitor |  |
|  +-----------+     +-------+-------+     +---------------+  |
|        |                   |                    ^           |
|        |                   v                    |           |
|        +---------------> Logger <---------------+           |
+-------------------------------------------------------------+
                              |
                              v
            +--------------------------------------+
            |      Control thread (non-block)      |
            | UNIX socket: /tmp/edgenetswitch.sock |
            +--------------------------------------+
                              |
                              v
                             CLI
```

## Daemon loop & message flow
- Startup: install signal handlers; load JSON config; initialize Logger, MessagingBus, Telemetry, HealthMonitor; open control socket and start control thread.
- Tick order: `telemetry.onTick()` publishes runtime metrics, then `health.onTick()` evaluates heartbeats; loop sleeps for the configured tick period.
- Heartbeat: Telemetry publishes uptime/tick counters; HealthMonitor consumes heartbeats and emits state transitions only on change.
- Shutdown: SIGINT/SIGTERM sets the stop flag, exits the loop, joins the control thread, publishes `SystemShutdown`, and closes the socket.
- Control lifecycle: control thread blocks on accept, serves read-only status snapshots, and terminates when the stop flag flips.

```cpp
std::atomic_bool stopFlag{false};

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "status") {
        return runStatusCLI() ? 0 : 1;
    }

    installSignalHandlers();
    auto cfg = ConfigLoader::loadFromFile("config/edgenetswitch.json");
    Logger::init(Logger::parseLevel(cfg.log.level), cfg.log.file);

    MessagingBus bus;
    Telemetry telemetry(bus, cfg);
    HealthMonitor health(bus, 500);

    int control_fd = createControlSocket();
    std::thread control(controlSocketThreadFunc, control_fd, std::cref(telemetry), std::cref(stopFlag));

    bus.publish({MessageType::SystemStart, nowMs()});
    while (!stopFlag.load(std::memory_order_relaxed)) {
        telemetry.onTick();
        health.onTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.daemon.tick_ms));
    }

    control.join();
    destroyControlSocket(control_fd);
    bus.publish({MessageType::SystemShutdown, nowMs()});
    Logger::shutdown();
}
```

## CLI usage (v1.1)
- Run the daemon: `./build/EdgeNetSwitchDaemon`
- Query status (from another shell): `./build/EdgeNetSwitchDaemon status`
- Example output: `uptime_ms=12345 tick_count=678`

## Testability and extensibility
- Deterministic tick loop and pure message passing make behavior reproducible and unit-testable.
- MessagingBus decouples producers and consumers, enabling new subsystems without touching the runtime loop.
- Control plane stays out-of-band and read-only, preserving timing while exposing observability.
- Architecture scales toward networking/orchestration by adding subscribers and control commands without rewriting the core.

## Build, run, test
```bash
git submodule update --init --recursive

cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

# Run daemon (reads config/edgenetswitch.json)
./build/EdgeNetSwitchDaemon

# Read-only status query
./build/EdgeNetSwitchDaemon status

# Unit tests
ctest --test-dir build
```

## Roadmap
- JSON-based control protocol
- Extended runtime status model
- Networking and switching subsystems
- Yocto/QEMU integration
- Kernel <-> user-space experiments
- Long-running soak tests

## Contact
[![LinkedIn - Murat Toğunçhan Düzgün](https://img.shields.io/badge/LinkedIn-Murat%20To%C4%9Fun%C3%A7han%20D%C3%BCzg%C3%BCn-blue.svg)](https://www.linkedin.com/in/togunchan/)
[![GitHub - togunchan](https://img.shields.io/badge/GitHub-togunchan-black.svg)](https://github.com/togunchan)
