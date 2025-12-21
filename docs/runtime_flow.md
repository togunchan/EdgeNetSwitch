# EdgeNetSwitch Runtime Flow

## Program entry point (`src/daemon/main.cpp`)
- Installs `SIGINT`/`SIGTERM` handlers that flip an atomic flag, letting the main loop exit cooperatively.
- Loads `Config` from `config/edgenetswitch.json`, then initializes the `Logger`.
- Builds the shared `MessagingBus`, then constructs `Telemetry` and `HealthMonitor` (timeout set to 500 ms).
- Registers bus subscribers for `SystemStart`, `SystemShutdown`, `Telemetry`, and `HealthStatus`; the `Telemetry` subscriber also forwards a heartbeat into `HealthMonitor`.
- Publishes `SystemStart` once the bus is wired, then enters the main loop.

## MessagingBus role and purpose
- Central in-process pub/sub channel; producers and consumers do not hold direct references to each other.
- Callback lists are guarded by a mutex and copied before invocation to avoid holding locks during handler execution.
- Supports multiple subscribers per `MessageType`, enabling logging, telemetry, and health logic to observe the same events without coupling.

## Message structure and payload concept
- Each `Message` carries a `MessageType`, a `timestamp_ms`, and a variant payload.
- Payloads are either `TelemetryData` (uptime, tick counter, timestamp) or `HealthStatus` (uptime, last heartbeat, alive flag); `std::monostate` represents an empty payload.
- Small illustration:
```cpp
struct Message {
    MessageType type;
    std::uint64_t timestamp_ms;
    using Payload = std::variant<std::monostate, TelemetryData, HealthStatus>;
    Payload payload{};
};
```

## SystemStart and SystemShutdown events
- `SystemStart` is published once by `main` immediately after subscribers are registered; currently only `main` logs it, but the bus allows other subsystems to join without code changes.
- `SystemShutdown` is published by `main` after the loop exits; again, `main` logs it, providing a single rendezvous point for teardown observers.

## Telemetry module behavior
- `Telemetry::onTick()` increments an internal counter, computes uptime from its construction time, and publishes a `Telemetry` message containing `TelemetryData`.
- Publisher: `Telemetry` module. Subscribers: `main` (for logging and to feed heartbeats into the health monitor); additional subscribers can be added via the bus without touching `Telemetry`.
- Example publication:
```cpp
TelemetryData data{/* uptime_ms */ nowMs() - start_time_ms_,
                   /* tick_count */ ++tick_count_,
                   /* timestamp_ms */ nowMs()};
bus_.publish({MessageType::Telemetry, data.timestamp_ms, data});
```

## HealthMonitor module behavior
- Tracks `last_heartbeat_ms_`, a configurable `timeout_ms_`, and the last published alive state.
- `onHeartbeat()` simply records the current time; `onTick()` computes whether the daemon is alive (`now - last_heartbeat_ms_ <= timeout_ms_`).
- Publishes `HealthStatus` only when the alive flag changes. Publisher: `HealthMonitor`. Subscriber: `main` logs transitions.
- Publishing only on state transitions keeps logs readable and mirrors embedded watchdogs where chatter is minimized until a health change occurs.

## Heartbeat mechanism and timeout logic
- Every `Telemetry` message causes the `main` subscriber to call `health.onHeartbeat()`, updating `last_heartbeat_ms_`.
- `HealthMonitor::onTick()` checks the elapsed time since the last heartbeat; if it exceeds `timeout_ms_` (500 ms in `main`), it publishes a single `HealthStatus{is_alive=false}`. A subsequent heartbeat flips it back to `true` with another single publish.
- This pattern ensures the health signal reflects liveness without flooding the bus or logs.

## Daemon main loop execution order
- Loop condition: runs while the atomic stop flag remains `false`.
- Per iteration order:
  1. `telemetry.onTick()` publishes `Telemetry`.
  2. `health.onTick()` evaluates heartbeat freshness and may publish `HealthStatus`.
  3. `std::this_thread::sleep_for(cfg.daemon.tick_ms)` (default 100 ms from config).
- The ordering guarantees heartbeats arrive before each health check within the same cycle.

## Graceful shutdown via signals
- `SIGINT` or `SIGTERM` sets the stop flag; the loop exits on the next check.
- After exit, `main` publishes `SystemShutdown`, logs the transition, and shuts down the logger, ensuring buffered output is flushed before process termination.

## Why this architecture is event-driven
- The bus decouples producers (e.g., `Telemetry`, `HealthMonitor`, lifecycle emitters) from consumers (loggers or future modules), so modules evolve independently.
- Messages carry time-stamped payloads, keeping state changes observable without shared mutable state.
- Event hooks (`SystemStart`, `Telemetry`, `HealthStatus`, `SystemShutdown`) create predictable join points for diagnostics or future control components.
- Copying subscriber lists before invocation prevents handler-side blocking from stalling publishers, improving resilience in a long-running daemon.

## How runtime behavior maps to unit tests
- `tests/messagingbus_tests.cpp` verifies single and multiple subscribers receive the correct `MessageType` events, matching the logging subscriptions in `main`.
- `tests/telemetry_tests.cpp` checks that `Telemetry::onTick()` publishes a `Telemetry` message each cycle and increments `tick_count`, mirroring the heartbeat source in the loop.
- `tests/health_monitor_tests.cpp` cover initial alive publication, heartbeat handling, and timeout to not-alive, aligning with the runtime health checks.
- `tests/health_monitor_transition_tests.cpp` ensure `HealthMonitor` publishes only on alive/not-alive transitions, validating the spam-prevention behavior relied upon by runtime logging.
