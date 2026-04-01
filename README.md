# EdgeNetSwitch

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Version](https://img.shields.io/badge/version-v1.8.2-orange)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)

Virtual embedded Linux edge device platform for deterministic, testable user-space daemons before real hardware, networking, or Yocto integration. Version: v1.8.2. Control protocol v1.2 (semantics stabilized).

## Why this exists
- Embedded/networking teams need a safe, repeatable target before boards, NICs, or BSPs exist.
- Focuses on deterministic daemon design with built-in observability so runtime behavior is explainable.
- Keeps architecture explicit and testable, enabling confident iteration before hardware bring-up.

## Quick Start
Minimal local run flow:

```bash
git clone https://github.com/togunchan/EdgeNetSwitch.git
cd EdgeNetSwitch
git submodule update --init --recursive

cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

./build/EdgeNetSwitchDaemon
```

The daemon runs a deterministic, tick-driven runtime loop.

External I/O such as telemetry export and control commands are isolated
from the runtime tick loop.

The control socket is exposed at:
`/tmp/edgenetswitch.sock`

Example query:

```bash
echo "1.2|status" | nc -U /tmp/edgenetswitch.sock
```

### UDP Example (v1.8)
(daemon must be running)
```bash
echo "id=42;payload=hello" | nc -u -w1 127.0.0.1 9000
```

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

### v1.4 – Control Protocol Stabilization
- Explicit protocol version validation (format and support).
- Backward compatibility enforcement for v1.2; unsupported versions are rejected.
- Well-defined error taxonomy with stable error_code values.
- Stable error encoding on the wire (ERR / error_code / message / END).
- No runtime mutation introduced; control plane remains read-only.

### v1.5 – Persistent Telemetry
- `FileTelemetryExporter` added and wired to write telemetry samples to `telemetry.log`.
- File output uses append mode (`std::ios::out | std::ios::app`) so existing log contents are preserved across restarts.
- `FileTelemetryExporter` uses an internal `std::mutex` to guard both `exportSample()` and teardown paths.
- RAII teardown is deterministic: destructor takes the same lock, then `flush()`es and `close()`s the file stream when open.
- Integrated into `TelemetryExportManager` during daemon startup as a third exporter beside stdout and in-memory exporters.
- No changes to `SnapshotPublisher` or memory ordering semantics in this version increment.
- No control protocol changes.
- The export path was later moved off the runtime path in v1.6.

### v1.6 – Asynchronous Telemetry Pipeline
- Telemetry export moved off the runtime path: runtime telemetry callbacks enqueue snapshots and continue the deterministic tick loop.
- Non-blocking enqueue model via `TelemetryExportManager::enqueue()` isolates runtime timing from exporter latency.
- Bounded queue introduced in `TelemetryExportManager` (default capacity `512`) to cap memory growth.
- Backpressure uses a drop-oldest policy when the queue is full; newest samples are retained.
- Dedicated export worker thread (`start()` / `stop()`) drains the queue and dispatches to all exporters.
- Worker wakeup and shutdown are coordinated with `std::condition_variable` to avoid busy-spinning.
- Queue backpressure metrics are attached to exported samples: `queue_size` and `dropped_samples` (`telemetry_queue_size` / `telemetry_dropped_samples` in `RuntimeMetrics`).

### v1.7 – Virtual Packet Pipeline
- Introduces a virtual data plane used to exercise packet processing behavior before real NIC or kernel integration.
- `PacketGenerator` emits synthetic packets and publishes `MessageType::PacketRx`.
- `PacketProcessor` subscribes to `MessageType::PacketRx`, processes packet payloads, and publishes `MessageType::PacketProcessed`.
- `PacketStats` subscribes to `MessageType::PacketProcessed` and accumulates packet counters.
- Event pipeline:

```text
PacketGenerator
    ↓
MessageType::PacketRx
    ↓
PacketProcessor
    ↓
MessageType::PacketProcessed
    ↓
PacketStats
```

- Packet metrics are exposed through the control plane:
  `echo "1.2|packet-stats" | nc -U /tmp/edgenetswitch.sock`
- Example response fields:
  `rx_packets`, `rx_bytes`, `drops`

### v1.8 – Real UDP Networking
- Introduces real UDP-based packet ingestion via `recvfrom()`.
- Configurable UDP ingress port (default `9000`).
- `PacketParser` validates and extracts packet fields from raw UDP data.
- UDP-ingested `PacketRx` events are timestamped at ingress via `nowMs()`.
- Both PacketGenerator (synthetic) and UDP ingress (real) produce PacketRx events that are published into the MessagingBus.
- Echo response implemented via `sendto()`.
- `PacketStats` is updated from real incoming packets.
- Event pipeline:

```text
UDP Socket
    ↓
PacketParser (validate)
    ↓
PacketRx (timestamp=nowMs)
    ↓
MessagingBus
```
- UDP ingress integrates with the existing packet pipeline introduced in v1.7.

### Packet Processing Pipeline (v1.8.1)
- UDP-based packet ingestion (POSIX sockets)
- Structured packet parsing (`PacketParser`)
- Separation of parse errors vs validation errors
- Event-driven drop handling via `MessagingBus` (`PacketDropped`)
- Packet processing stage (`PacketProcessor`)
- Payload vs wire size separation (network vs processing layer)
- Real-time packet metrics (`PacketStats`)
- Control-plane visibility via `packet-stats` command
- Improves observability and separation of concerns in the packet pipeline.

### Packet Rate Telemetry (v1.8.2)
- Naive per-snapshot rates are unstable under bursty ingress and scheduler phase offset; packet arrival is not synchronized with the tick boundary.
- `PacketStats` now computes packet/byte rates with a time gate (`>=1000ms`) to avoid sub-window jitter amplification.
- EWMA smoothing (`alpha=0.2`) is applied for control-plane interpretability; it filters variance but does not redefine measurement correctness.
- Dual observability is exposed: raw interval rates (`rx_packets_per_sec_raw`, `rx_bytes_per_sec_raw`) and smoothed trend rates (`rx_packets_per_sec`, `rx_bytes_per_sec`).
- Deterministic rate validation is supported via `snapshotAt(now_ms)` for explicit time control in tests (no wall-clock dependency).
- CLI example:

```text
OK
rx_packets=321
rx_bytes=2353
rx_packets_per_sec=12
rx_bytes_per_sec=88
rx_packets_per_sec_raw=9
rx_bytes_per_sec_raw=76
END
```
- Raw rates reflect the last measurement window and may fluctuate due to scheduling jitter and burst alignment.
- Smoothed rates (EWMA) provide a stable control-plane signal suitable for trend interpretation.
- The divergence between raw and smoothed rates indicates transition dynamics in traffic behavior.

### Intentionally not included (as of v1.8)
- Runtime mutation or control commands
- Raw NIC driver or kernel data-plane integration
- Yocto or QEMU integration
- Remote TCP/IP management
- Authentication or authorization
- Hard real-time guarantees
Deferred to keep the runtime deterministic, avoid premature coupling to hardware-specific data-plane stacks, and preserve a minimal, observable surface while the control plane remains read-only and stable.

## Runtime & control-plane architecture
The tick-driven runtime owns execution while all subsystems communicate via the in-process MessagingBus. An out-of-band control thread exposes read-only inspection over a UNIX socket using the versioned control protocol (v1.2), dispatches commands through a metadata-driven table, and returns framed responses without pausing the runtime. Telemetry export is isolated behind a bounded asynchronous queue so runtime ticks do not block on exporter I/O.

### Packet Flow (v1.8.2)

UDP Receiver → Parser → Validator → Processor → MessagingBus → Stats

```
+-------------------------------------------+
| Network Ingress (v1.8)                    |
|   UDP Socket (recvfrom)                   |
|        -> PacketParser (validate)         |
|        -> PacketRx (timestamp=nowMs)      |
+--------------------+----------------------+
                     | (event injection)
                     v
+------------------------------------------------------------------------------------+
| Runtime Plane (deterministic tick loop)                                            |
|                                                                                    |
|  tick -> Telemetry ----------+                                                     |
|       -> HealthMonitor       |                                                     |
|       -> PacketGenerator ----+                                                     |
|                              |                                                     |
|  UDP Ingress (recvfrom) -----+--> PacketRx ---->                                   |
|                                                v                                   |
|                            +-----------------------+                               |
|                            |      MessagingBus     |  internal event backbone      |
|                            +-----+-----------+-----+                               |
|                                  |           |                                     |
|                                  |           +--> PacketProcessor                  |
|                                  |                 |                               |
|                                  |                 +--PacketProcessed-->PacketStats|
|                                  |                                                 |
|                                  +--> Telemetry sample -> RuntimeMetrics           |
|                                                                                    |
|  Telemetry + HealthMonitor + PacketStats                                           |
|                               v                                                    |
|                        Runtime snapshot                                            |
|                               v                                                    |
|                        SnapshotPublisher                                           |
+------------------------------------------------------------------------------------+
                 |                                            |
                 v                                            v
+-------------------------------------------+   +------------------------------------+
| Export Path (off tick thread)             |   | Control Plane (out-of-band)        |
| TelemetryExportManager (bounded queue)    |   | Control thread                     |
|   -> export worker thread                 |   |   -> UNIX socket                   |
|   -> exporters (stdout / memory / file)   |   |      /tmp/edgenetswitch.sock       |
+-------------------------------------------+   |   -> CLI / edgenetctl / nc         |
                                                +------------------------------------+
```

## Daemon loop & message flow
- Startup: install signal handlers; load JSON config; initialize Logger, MessagingBus, Telemetry, HealthMonitor, `TelemetryExportManager`; open control socket; start control thread and export worker thread.
- Tick order: `telemetry.onTick()` publishes runtime metrics, then `health.onTick()` evaluates heartbeats; loop sleeps for the configured tick period.
- Telemetry export path: `MessageType::Telemetry` callbacks build `RuntimeMetrics` and call `exportManager.enqueue()` (non-blocking); when full, queue backpressure drops the oldest sample.
- Export worker: waits on `std::condition_variable`, drains queued samples, and invokes registered exporters off the runtime path.
- Heartbeat: Telemetry publishes uptime/tick counters; HealthMonitor consumes heartbeats and emits state transitions only on change.
- Shutdown: SIGINT/SIGTERM sets the stop flag, exits the loop, joins the control thread, stops/joins the export worker thread, publishes `SystemShutdown`, and closes the socket.
- Control lifecycle: control thread blocks on accept, parses `version|command` requests, dispatches through the metadata-driven table, returns framed responses (`OK`/`ERR` + payload + `END`), and terminates when the stop flag flips.

## Daemon loop & message flow (simplified)
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
        telemetry.onTick();   // produce metrics
        health.onTick();      // evaluate system health
        // packet pipeline runs via MessagingBus events
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.daemon.tick_ms));
    }

    control.join();
    destroyControlSocket(control_fd);
    bus.publish({MessageType::SystemShutdown, nowMs()});
    Logger::shutdown();
}
```

## CLI usage (v1.8)
- Run the daemon: `./build/EdgeNetSwitchDaemon`
- Query status: `./build/EdgeNetSwitchDaemon status`
- Query health: `./build/EdgeNetSwitchDaemon health`
- Query metrics: `./build/EdgeNetSwitchDaemon metrics`
- Query packet statistics: `./build/EdgeNetSwitchDaemon packet-stats`
- Query version: `./build/EdgeNetSwitchDaemon version`
- Help summary: `./build/EdgeNetSwitchDaemon help`
- Help for a command (detailed introspection): `./build/EdgeNetSwitchDaemon help <command>`
- Help for a command (alternate form): `./build/EdgeNetSwitchDaemon help:<command>`
- Help output is generated from dispatch metadata in the daemon.
- Request format: `1.2|<command>`
- Response framing: `OK` or `ERR` header line, payload lines, terminator `END` (errors include `error_code` and `message`)
- `status` payload: `state`, `uptime_ms`, `tick_count`
- `health` payload: `alive`, `timeout_ms`
- `metrics` payload: `uptime_ms`, `tick_count`
- `version` payload: `version`, `protocol`, `build`

### Control Protocol Error Model
Errors are part of the protocol contract, not implementation details. Each error carries a stable `error_code` and a human-readable `message`.

- `invalid_request`: missing version or command, or malformed request line.
- `invalid_version_format`: protocol version is not in `digit.digit` form (e.g., `1.2`).
- `unsupported_version`: version is well-formed but not supported (only `1.2`).
- `unknown_command`: command is not present in the dispatch table.
- `internal_error`: fallback when a response lacks a specific error_code.

### CLI / IPC examples
```text
# Valid request
> 1.2|status
< OK
< state=<state>
< uptime_ms=<ms>
< tick_count=<count>
< END

# Unsupported version
> 1.3|status
< ERR
< error_code=unsupported_version
< message=unsupported protocol version: 1.3
< END

# Unknown command
> 1.2|frobnicate
< ERR
< error_code=unknown_command
< message=unknown command: frobnicate
< END
```

## Design principles
- Read-only control plane; no runtime mutation.
- Deterministic runtime loop unchanged.
- Protocol versioning enables forward-compatible evolution without breaking v1.2 semantics.

## Testability and extensibility
- Deterministic tick loop and pure message passing make behavior reproducible and unit-testable.
- MessagingBus decouples producers and consumers, enabling new subsystems without touching the runtime loop.
- Control plane stays out-of-band and read-only, preserving timing while exposing observability.
- Centralized control dispatch isolates command handling from socket I/O while keeping the runtime loop unchanged.
- Adding a new control command requires only extending the dispatch table; no socket or CLI changes are needed.
- Dispatch metadata is the single source of truth for command definitions and help output.

### Packet Pipeline Testing
Packet pipeline behavior is validated using Catch2 unit tests.

- packet event propagation through `MessagingBus`
- packet metric accumulation
- invalid payload handling
- pipeline stability across multiple packets

## Project Goals
- Deterministic daemon architecture.
- Explicit runtime/control-plane separation.
- Observable runtime behavior through control-plane snapshots and telemetry.
- Testable subsystems with focused unit tests.
- Architecture experimentation before hardware exists.

## Non-Goals
The project intentionally avoids:

- Production network switching.
- Kernel driver development.
- Hardware BSP integration.
- Real-time guarantees.

These may appear in later stages as the architecture matures.

## Project Status
EdgeNetSwitch is an experimental systems architecture project exploring
deterministic daemon design for edge devices.

The runtime core and control protocol are stable.

Current development focuses on hardening the UDP-integrated packet pipeline,
which is now driven by real network traffic, and transitioning the system
from simulation-oriented flows toward broader real I/O integration.

The system has transitioned from fully synthetic packet simulation (v1.7)
to hybrid operation with real UDP-based packet ingress (v1.8).

Current focus is on stabilizing real I/O integration while preserving
deterministic runtime guarantees, preparing the architecture for
stateful packet forwarding (v1.9).

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

## Contributing
EdgeNetSwitch is primarily a personal systems architecture project, but contributions and technical discussion are welcome.

Open an issue for:

- Architecture ideas
- Runtime experiments
- Documentation improvements

## Contact
[![LinkedIn - Murat Toğunçhan Düzgün](https://img.shields.io/badge/LinkedIn-Murat%20To%C4%9Fun%C3%A7han%20D%C3%BCzg%C3%BCn-blue.svg)](https://www.linkedin.com/in/togunchan/)
[![GitHub - togunchan](https://img.shields.io/badge/GitHub-togunchan-black.svg)](https://github.com/togunchan)
