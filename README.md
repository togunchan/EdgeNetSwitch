# EdgeNetSwitch

Virtual embedded Linux edge device platform focused on a deterministic, testable user-space runtime. EdgeNetSwitch models how industrial edge firmware is structured before adding real hardware, networking, or Yocto integration.

## Why this exists
- Embedded and networking teams often lack safe, repeatable environments to prototype daemon architectures before hardware arrives. EdgeNetSwitch provides that target.
- The platform demonstrates how to structure a long-lived, event-driven control plane with clean separation between logging, configuration, telemetry, and health without tight coupling.
- Everything is designed to be observable and unit-testable so behavior can be validated the same way you would validate production firmware.

## Scope: v1.0 (runtime core only)
- Long-lived daemon with deterministic tick loop.
- Thread-safe Logger with file + level control via JSON config.
- In-process MessagingBus (pub/sub) with mutex-protected fan-out.
- JSON ConfigLoader for runtime parameters (e.g., tick period, log level/file).
- Telemetry publisher (uptime + tick counter).
- HealthMonitor with heartbeat-based liveness detection.
- Signal-aware lifecycle hooks that publish `SystemStart` and `SystemShutdown`.
- All components covered by Catch2 unit tests.

### Intentionally **not** in v1.0
- No networking stack or packet forwarding yet.
- No Yocto build, QEMU image, or kernel drivers in the runtime path.
- No persistence, RPC/CLI surface, or distributed messaging layer.
- No hard real-time guarantees.

## Runtime architecture
EdgeNetSwitch is event-driven: every subsystem talks through the in-process MessagingBus. The daemon owns the loop and the lifecycle; modules remain isolated and replaceable.

```
+------------------------------------------------------------+
| EdgeNetSwitch Daemon (C++20, tick-driven)                  |
|                                                            |
|  +-----------------+     +-----------------+               |
|  | ConfigLoader    |-->  | MessagingBus    |<-- signals    |
|  +-----------------+     +--------+--------+               |
|                              ^    ^                        |
|                              |    |                        |
|                   Telemetry --    -- HealthMonitor         |
|                              \    /                        |
|                              Logger                        |
+------------------------------------------------------------+
```

### Modules at a glance
- `Logger`: thread-safe sink; configured at startup; used across subscribers.
- `MessagingBus`: pub/sub fan-out keyed by `MessageType`; copies subscriber lists before dispatch to avoid holding locks during callbacks.
- `ConfigLoader`: JSON source of truth (tick interval, log settings).
- `Telemetry`: emits uptime + tick count each loop.
- `HealthMonitor`: tracks last heartbeat; publishes transitions on timeout.
- `main`: installs signal handlers, wires subscriptions, publishes lifecycle events, and runs the deterministic loop.

## Daemon loop & message flow
- Lifecycle:
  - Load `config/edgenetswitch.json`, init Logger, construct MessagingBus/Telemetry/HealthMonitor.
  - Subscribe logger/health handlers to `SystemStart`, `Telemetry`, `HealthStatus`, `SystemShutdown`.
  - Publish `SystemStart`, then enter the tick loop. Signals (`SIGINT`/`SIGTERM`) flip an atomic flag to exit.
- Per-iteration order:
  1. `telemetry.onTick()` → publishes `Telemetry`.
  2. `health.onTick()` → publishes `HealthStatus` only on state transitions.
  3. `sleep(cfg.daemon.tick_ms)` (default 100 ms).
- Heartbeats: The Telemetry subscriber calls `health.onHeartbeat()` so `HealthMonitor` only warns when heartbeats stop exceeding its timeout (500 ms by default).
- Shutdown: loop exit publishes `SystemShutdown`, logs, then flushes and shuts down the logger.

High-level loop sketch:

```cpp
installSignalHandlers();
auto cfg = ConfigLoader::loadFromFile("config/edgenetswitch.json");
Telemetry telemetry(bus, cfg);
HealthMonitor health(bus, /*timeout_ms=*/500);

bus.publish({MessageType::SystemStart, nowMs()});
while (!stop_requested) {
    telemetry.onTick();   // publishes Telemetry
    health.onTick();      // publishes HealthStatus on transitions
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.daemon.tick_ms));
}
bus.publish({MessageType::SystemShutdown, nowMs()});
```

## Testability and extensibility
- Deterministic tick loop and pure-message interactions make modules easy to unit test; every core component ships with Catch2 tests.
- MessagingBus decouples producers/consumers, so new subsystems (e.g., routing logic, CLI adapters) can subscribe without touching existing code.
- Heartbeat-based health keeps logs quiet until liveness changes, mirroring watchdog patterns common in embedded systems.
- JSON config keeps runtime parameters mutable without recompiling.

## Build, run, test
```bash
# Fetch dependencies
git submodule update --init --recursive

# Configure and build
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

# Run daemon (reads config/edgenetswitch.json)
./build/EdgeNetSwitchDaemon

# Execute unit tests
ctest --test-dir build
```

## Roadmap
- Yocto layer and image recipes to bake the daemon into a reproducible rootfs.
- QEMU automation for ARM64 bring-up and end-to-end boot tests.
- Networking stack simulation (routing/switching flows, control-plane hooks).
- Kernel modules and pseudo-drivers with user-space integration paths.
- Control/inspection surface (CLI or RPC) backed by the MessagingBus.
- Integration and soak tests that exercise startup, heartbeat loss, and recovery.

## Contact
Questions, feedback, ideas?

[![LinkedIn](https://img.shields.io/badge/LinkedIn-Murat_Toğunçhan_Düzgün-blue.svg)](https://www.linkedin.com/in/togunchan/)
[![GitHub](https://img.shields.io/badge/GitHub-togunchan-black.svg)](https://github.com/togunchan)
