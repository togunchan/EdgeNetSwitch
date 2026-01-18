# EdgeNetSwitch

Virtual embedded Linux edge device platform for deterministic, testable user-space daemons before real hardware, networking, or Yocto integration. Version: v1.3.0 (control protocol v1.2).

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
- Initial CLI command: status
- Live telemetry snapshot retrieval
- Clean separation between runtime, control plane, and CLI
- Graceful shutdown coordination between threads

### v1.2 – Control Protocol v1.2
- Versioned control request/response protocol (v1.2)
- Framed responses (OK / ERR / END)
- Centralized command dispatch layer
- CLI commands: status, health, metrics, version
- Structured key=value payloads for control responses

### v1.3 – Control Plane Introspection & Dispatch Refinement
- Metadata-driven dispatch table via `CommandDescriptor`; new commands are registered only through the table.
- Command introspection via `help`, with detailed `help <command>` and `help:<command>` output derived entirely from dispatch metadata.
- Unified handler signatures: `(const ControlContext&, const std::string& arg)`.
- Cleaner CLI/daemon separation: CLI is presentation-only while all logic lives in the daemon.
- Header hygiene improved using forward declarations.
- No IPC protocol changes; control protocol remains v1.2.
- Extensibility improvements without runtime impact; existing command behavior and output are unchanged.

### Intentionally not included (as of v1.3)
- Runtime mutation or control commands
- Networking / switching logic
- Yocto or QEMU integration
- Remote TCP/IP management
- Authentication or authorization
- Hard real-time guarantees
Deferred to keep the runtime deterministic, avoid premature coupling to hardware/network stacks, and preserve a minimal, observable surface while the control plane stabilizes.

## Runtime & control-plane architecture
The tick-driven runtime owns execution while all subsystems communicate via the in-process MessagingBus. An out-of-band control thread exposes read-only inspection over a UNIX socket using the versioned control protocol (v1.2), dispatches commands through a metadata-driven table, and returns framed responses without pausing the runtime.

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
- Control lifecycle: control thread blocks on accept, parses `version|command` requests, dispatches through the metadata-driven table, returns framed responses (`OK`/`ERR` + payload + `END`), and terminates when the stop flag flips.

```cpp
std::atomic_bool stopFlag{false};

int main(int argc, char** argv) {
    if (argc > 1) {
        runControlCLI(argv[1]);
        return 0;
    }

    installSignalHandlers();
    auto cfg = ConfigLoader::loadFromFile("config/edgenetswitch.json");
    Logger::init(Logger::parseLevel(cfg.log.level), cfg.log.file);

    MessagingBus bus;
    RuntimeState runtimeState = RuntimeState::Booting;
    Telemetry telemetry(bus, cfg);
    HealthMonitor health(bus, 500);

    int control_fd = createControlSocket();
    std::thread control(controlSocketThreadFunc,
                        control_fd,
                        std::cref(telemetry),
                        std::cref(runtimeState),
                        std::cref(health),
                        std::cref(stopFlag));

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

## CLI usage (v1.3)
- Run the daemon: `./build/EdgeNetSwitchDaemon`
- Query status: `./build/EdgeNetSwitchDaemon status`
- Query health: `./build/EdgeNetSwitchDaemon health`
- Query metrics: `./build/EdgeNetSwitchDaemon metrics`
- Query version: `./build/EdgeNetSwitchDaemon version`
- Help summary: `./build/EdgeNetSwitchDaemon help`
- Help for a command (detailed introspection): `./build/EdgeNetSwitchDaemon help <command>`
- Help for a command (alternate form): `./build/EdgeNetSwitchDaemon help:<command>`
- Help output is generated from dispatch metadata in the daemon.
- Request format: `1.2|<command>`
- Response framing: `OK` or `ERR` header line, payload lines, terminator `END`
- `status` payload: `state`, `uptime_ms`, `tick_count`
- `health` payload: `alive`, `timeout_ms`
- `metrics` payload: `uptime_ms`, `tick_count`
- `version` payload: `version`, `protocol`, `build`

## Testability and extensibility
- Deterministic tick loop and pure message passing make behavior reproducible and unit-testable.
- MessagingBus decouples producers and consumers, enabling new subsystems without touching the runtime loop.
- Control plane stays out-of-band and read-only, preserving timing while exposing observability.
- Centralized control dispatch isolates command handling from socket I/O while keeping the runtime loop unchanged.
- Adding a new control command requires only extending the dispatch table; no socket or CLI changes are needed.
- Dispatch metadata is the single source of truth for command definitions and help output.

## Build, run, test
```bash
git submodule update --init --recursive

cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

# Run daemon (reads config/edgenetswitch.json)
./build/EdgeNetSwitchDaemon

# Read-only control queries
./build/EdgeNetSwitchDaemon status
./build/EdgeNetSwitchDaemon health
./build/EdgeNetSwitchDaemon metrics
./build/EdgeNetSwitchDaemon version

# Unit tests
ctest --test-dir build
```

## Contact
[![LinkedIn - Murat Toğunçhan Düzgün](https://img.shields.io/badge/LinkedIn-Murat%20To%C4%9Fun%C3%A7han%20D%C3%BCzg%C3%BCn-blue.svg)](https://www.linkedin.com/in/togunchan/)
[![GitHub - togunchan](https://img.shields.io/badge/GitHub-togunchan-black.svg)](https://github.com/togunchan)
