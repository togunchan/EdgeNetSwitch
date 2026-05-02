# Changelog

This changelog tracks milestone-level evolution of EdgeNetSwitch, a deterministic
C++20 runtime for simulating embedded network systems. It focuses on architectural
intent, operational behavior, and correctness boundaries. Trivial cleanup,
formatting, and incidental documentation changes are intentionally omitted.

## Evolution Overview

EdgeNetSwitch evolves in clear architectural phases:
- `v1.0.0` through `v1.4.0` establish the deterministic runtime core, lifecycle events, read-only control plane, and stable protocol semantics.
- `v1.5.0` through `v1.6.0` isolate observability from runtime timing by moving telemetry persistence and export behind bounded asynchronous handoff.
- `v1.7.0` through `v1.8.1` introduce the packet pipeline first as a synthetic data-plane model, then as controlled real UDP ingress.
- `v1.8.2` through `v1.8.5` mature the system under operational pressure with interpretable telemetry, JSON control responses, bounded admission, explicit backpressure, and documented concurrency semantics.
- `v1.8.7` elevates the packet path from observable behavior to a formally auditable correctness model with lifecycle-based accounting.

The system evolves from a deterministic simulation core into a correctness-driven runtime with explicit boundaries for concurrency, observability, and network behavior.

## [v1.8.7] - Correctness via Lifecycle-Based Accounting

### Added
- Introduced `lifecycle_id` as the runtime-owned identity for one observed packet lifecycle.
- Added terminal accounting fields for `terminal_events`, `pending_terminal_events`, and lifecycle-scoped `duplicate_events`.
- Propagated lifecycle identity through `PacketRx`, `PacketProcessed`, and all `PacketDropped` outcomes.

### Changed
- Reclassified `packet.id` as payload identity supplied by traffic, not as a runtime accounting key.
- Updated `PacketStats` to enforce lifecycle correctness using runtime-assigned identifiers.
- Defined packet accounting invariants:
  - `terminal_events == processed_packets + total_drops`
  - `ingress_packets == terminal_events + pending_terminal_events`

### Fixed
- Corrected duplicate-event detection so reused sender packet IDs no longer appear as runtime lifecycle violations.
- Prevented ingress-to-terminal accounting from depending on externally supplied packet metadata.

### Engineering Notes
- This release moves packet observability from counter aggregation to auditable lifecycle accounting.
- The design separates transport payload semantics from runtime ownership, which is required for replay, retry, malformed input, and sender-controlled IDs.
- The result is a stronger correctness boundary for asynchronous packet processing: every lifecycle is either terminal or explicitly pending.
- This establishes a correctness contract independent of transport-level behavior.

## [v1.8.5] - Stabilizing Asynchronous Runtime Semantics

### Added
- Added lifecycle completeness tracking and duplicate-terminal detection for asynchronous packet flows.
- Documented the threading model, including synchronous `MessagingBus` dispatch and explicit async handoff points.
- Added concurrent overload coverage for the packet pipeline.

### Changed
- Replaced polling-oriented packet worker behavior with condition-variable-driven queue wakeups.
- Hardened shutdown sequencing across daemon, UDP receiver, control socket, telemetry export, and packet worker ownership.
- Updated async tests to assert eventual convergence rather than synchronous-era immediate state.

### Fixed
- Removed busy-wait behavior from packet processing.
- Fixed timing-dependent teardown races around control socket and UDP shutdown.
- Stabilized packet lifecycle tests under concurrent overload.

### Engineering Notes
- This milestone makes concurrency semantics explicit instead of accidental.
- The project now distinguishes synchronous in-process event dispatch from queue-backed async execution, which is essential for reasoning about thread affinity and shutdown.
- Correctness under load is validated through lifecycle completion, not by assuming immediate callback completion.

## [v1.8.4] - Explicit Backpressure and Bounded Admission

### Added
- Added a bounded `PacketProcessor` work queue with `MAX_QUEUE_SIZE = 1024`.
- Added a dedicated packet worker thread for asynchronous processing.
- Added `QueueOverflow` as a first-class `PacketDropped` reason.
- Exposed pressure metrics through `packet-stats`, including `ingress_packets`, `processed_packets`, `processing_gap`, and structured drops by reason.

### Changed
- Changed `PacketRx` handling from synchronous processing to admission control.
- Packet ingress now either enqueues accepted work or emits an explicit overload drop.
- Control-plane packet statistics now expose backlog and saturation rather than only completed throughput.

### Fixed
- Eliminated the architectural blind spot where synchronous callbacks hid overload by pacing ingress with processing latency.
- Made queue saturation visible as loss instead of unbounded latency or implicit callback slowdown.

### Engineering Notes
- A queue does not make the system faster; it defines an honest capacity boundary.
- This release separates offered load, admitted work, completed work, and overload loss as distinct operational states.
- `QueueOverflow` is intentionally distinct from parse and validation errors so operators can attribute packet loss to admission pressure instead of malformed input.

## [v1.8.3] - Automation-Ready Control Plane Semantics

### Added
- Standardized control-plane responses as JSON with `status`, `data`, and structured `error` objects.
- Added shared JSON response helpers across control handlers.
- Added JSON-aware CLI parsing and proper process exit codes.
- Added control-plane tests covering JSON response behavior.

### Changed
- Made `:json` the supported output modifier for command arguments.
- Removed legacy framed-response parsing from CLI logic.
- Unified control handler output semantics across status, health, metrics, version, and packet statistics.

### Fixed
- Improved error consistency for unknown commands, invalid requests, malformed versions, and unsupported protocol versions.
- Strengthened configuration parsing validation used by the control layer.

### Engineering Notes
- This release moves the control plane from human-oriented text output to an automation-friendly contract.
- JSON responses make observability output suitable for dashboards, tests, and future tooling.
- The runtime remains read-only from the control plane, preserving deterministic execution while improving inspection fidelity.

## [v1.8.2] - Interpretable Packet Rate Telemetry

### Added
- Added packet and byte rate telemetry for packet statistics.
- Exposed both raw interval rates and EWMA-smoothed rates.
- Added deterministic time control through `snapshotAt(now_ms)` for rate validation without wall-clock dependency.
- Added EWMA validation tests and reset-safety coverage.

### Changed
- Moved rate calculation to a gated update model with a minimum one-second measurement window.
- Introduced EWMA smoothing with `alpha = 0.2` for control-plane readability under bursty traffic.

### Fixed
- Reduced misleading per-snapshot volatility caused by scheduler jitter, burst alignment, and tick-to-arrival phase offset.

### Engineering Notes
- Rates are derivative telemetry; cumulative counters remain the canonical integrity signal.
- Raw rates expose observed interval truth, while EWMA rates provide a stable trend signal for operators.
- The design deliberately trades immediate responsiveness for observability stability and should not be used for real-time control decisions.

## [v1.8.1] - Stage-Aware Packet Pipeline Observability

### Added
- Added `PacketValidator` to separate parsing success from semantic validation.
- Added event-driven `PacketDropped` handling through `MessagingBus`.
- Added detailed packet metrics for parse errors, validation errors, and drop totals.
- Added packet source metadata, including source IP and port.

### Changed
- Split packet size accounting into wire size and payload size.
- Moved packet statistics collection to bus subscribers rather than embedding it in producer logic.
- Standardized packet-pipeline logging for clearer runtime observability.

### Fixed
- Prevented malformed input and validation failures from collapsing into a single undifferentiated failure class.

### Engineering Notes
- This release makes packet-path stage ownership visible through structured events.
- Separating parse, validation, drop, and processing events improves root-cause attribution without violating deterministic execution guarantees.
- Wire-size versus payload-size accounting keeps network-layer and processing-layer metrics distinct.

## [v1.8.0] - Controlled Real Network Ingress

### Added
- Added real UDP packet ingestion through `UdpReceiver` and `recvfrom()`.
- Added configurable UDP ingress port with default port `9000`.
- Added packet parsing, validation, timestamping, formatted logging, and UDP echo response.
- Added graceful lifecycle handling for the UDP receiver.

### Changed
- Integrated real UDP ingress into the same `MessagingBus` packet path used by the synthetic packet pipeline.
- Promoted the runtime from fully synthetic packet simulation to hybrid operation with real network input.

### Fixed
- Hardened packet parser behavior with stricter validation and unit coverage.

### Engineering Notes
- This milestone introduces real I/O without allowing external networking to own runtime timing.
- UDP input enters through a controlled event boundary, preserving the deterministic runtime model while enabling realistic ingress behavior.
- The design keeps hardware and kernel integration deferred while validating network-facing runtime behavior early.

## [v1.7.0] - Synthetic Data Plane as a Runtime Test Surface

### Added
- Added a synthetic packet generator that publishes `PacketRx` events.
- Added `PacketProcessor` and `PacketStats` subscribers for packet processing and metrics accumulation.
- Added `packet-stats` visibility through the control plane.

### Changed
- Extended the runtime from system telemetry simulation into a virtual data-plane model.
- Established the event path `PacketGenerator -> PacketRx -> PacketProcessor -> PacketProcessed -> PacketStats`.

### Engineering Notes
- This release creates a controlled packet-processing surface before real NIC, kernel, or BSP integration exists.
- The virtual pipeline makes packet behavior testable inside the deterministic runtime and prepares the architecture for real ingress in v1.8.

## [v1.6.0] - Isolating Telemetry I/O from Runtime Determinism

### Added
- Added non-blocking telemetry enqueue through `TelemetryExportManager::enqueue()`.
- Added a bounded telemetry export queue with default capacity `512`.
- Added a dedicated export worker thread with condition-variable wakeup and shutdown coordination.
- Added telemetry queue pressure metrics for queue size and dropped samples.

### Changed
- Moved telemetry export off the runtime tick path.
- Applied drop-oldest backpressure so newest telemetry samples are retained when the export queue is saturated.

### Fixed
- Prevented slow exporters from blocking deterministic runtime ticks.
- Capped memory growth in the telemetry path under exporter latency or downstream stalls.

### Engineering Notes
- This release formalizes the runtime/export boundary.
- The design favors deterministic tick execution over lossless telemetry export, which is the correct tradeoff for a runtime simulator.
- Queue pressure is surfaced explicitly so dropped telemetry samples are observable rather than hidden.

## [v1.5.0] - Durable Telemetry as an Operational Trail

### Added
- Added `FileTelemetryExporter` for persistent telemetry output to `telemetry.log`.
- Added append-mode file output so telemetry survives daemon restarts.
- Added mutex protection around export and teardown paths.
- Added deterministic RAII cleanup with flush and close on destruction.

### Changed
- Integrated file export into `TelemetryExportManager` alongside stdout and in-memory exporters.

### Engineering Notes
- Persistent telemetry provides a replayable operational trail without changing runtime state.
- This release intentionally kept the export path simple; moving export work off the runtime path was deferred to v1.6 once persistence semantics were established.

## [v1.4.0] - Stable Error Semantics for Control Protocols

### Added
- Added explicit control protocol version validation.
- Added stable error codes for malformed requests, unsupported versions, unknown commands, and internal errors.
- Added stable wire encoding for protocol errors.

### Changed
- Enforced backward compatibility for supported protocol `v1.2`.
- Rejected unsupported protocol versions instead of allowing ambiguous command handling.

### Fixed
- Removed ambiguity between malformed requests and unsupported protocol versions.

### Engineering Notes
- This release treats errors as part of the protocol contract rather than incidental strings.
- Stable failure semantics are required before external tooling can safely automate against the control plane.
- The control plane remained read-only to avoid introducing runtime mutation while protocol behavior was being stabilized.

## [v1.3.0] - Metadata-Driven Control Plane Extensibility

### Added
- Added metadata-driven command descriptors through `CommandDescriptor`.
- Added `help`, `help <command>`, and `help:<command>` output generated from dispatch metadata.
- Added unified control handler signatures using `ControlContext` and command arguments.

### Changed
- Centralized command registration in the dispatch table.
- Kept CLI behavior presentation-only while daemon-side dispatch retained command logic.
- Improved header hygiene through forward declarations.

### Engineering Notes
- This milestone makes the control plane extensible without touching socket handling or CLI presentation.
- Dispatch metadata becomes the single source of truth for command names, arguments, descriptions, and fields.
- No protocol change was required, preserving v1.2 compatibility while improving maintainability.

## [v1.2.0] - Protocol Versioning as an Evolution Boundary

### Added
- Added versioned control requests using the `version|command` protocol form.
- Added CLI/control commands for `status`, `health`, `metrics`, and `version`.
- Added structured key-value payloads for control responses.
- Added initial `OK` / `ERR` / `END` response framing.

### Changed
- Introduced a centralized command dispatch layer between socket I/O and command handlers.

### Engineering Notes
- This release gives the control plane an explicit compatibility surface.
- Versioned requests allow protocol behavior to evolve without coupling every change to daemon internals.
- The initial framed response model was sufficient for shell inspection and later replaced by JSON semantics in v1.8.3.

## [v1.1.0] - Out-of-Band Runtime Inspection

### Added
- Added a UNIX domain socket control plane at `/tmp/edgenetswitch.sock`.
- Added a dedicated control thread so inspection does not block the runtime loop.
- Added read-only runtime inspection through the initial `status` command.
- Added live telemetry snapshot retrieval.
- Added graceful coordination between runtime and control threads.

### Changed
- Separated runtime execution, control-plane access, and CLI presentation.

### Engineering Notes
- This release introduces out-of-band observability as a core architectural principle.
- Runtime state can be inspected without allowing the control plane to mutate behavior or pause deterministic execution.
- The dedicated control thread establishes the pattern later used for other isolated I/O paths.

## [v1.0.0] - Deterministic Runtime Foundation

### Added
- Added deterministic tick-driven daemon execution.
- Added thread-safe logging with JSON configuration.
- Added in-process `MessagingBus` pub/sub communication.
- Added JSON configuration loading.
- Added telemetry for uptime and tick count.
- Added heartbeat-based health monitoring.
- Added `SystemStart` and `SystemShutdown` lifecycle events.
- Added Catch2 unit coverage for core runtime components.

### Engineering Notes
- This release establishes the simulator's core contract: deterministic runtime behavior with explicit subsystem boundaries.
- The in-process event bus provides decoupling without introducing distributed-system complexity too early.
- The architecture starts with lifecycle, telemetry, health, and testability before hardware, kernel, or data-plane integration.

## Notes

- This changelog focuses on architectural milestones, system behavior, and engineering tradeoffs rather than commit-level detail.
- Full commit-level history, including intermediate and experimental changes, is maintained in Git.
