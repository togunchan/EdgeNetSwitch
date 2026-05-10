# EdgeNetSwitch — Architecture Overview

EdgeNetSwitch is a virtual embedded Linux device platform designed to simulate
industrial edge hardware entirely inside QEMU. It provides a complete, modular,
and hardware-independent development environment for:

- Custom Linux OS images (Yocto-based)
- Kernel modules and pseudo-device drivers
- User-space daemons written in modern C++20
- Networking and routing simulation
- Telemetry, logging, and system inspection tools

The goal is to replicate the real-world engineering workflows used by industrial
embedded teams (e.g., Siemens, Bosch, ABB) while keeping the system fully
reproducible, testable, and open for experimentation.

---

## Project Goals

EdgeNetSwitch enables full embedded Linux development without physical hardware.
Its primary objectives are:

- Provide a realistic virtual embedded environment for OS, driver, and daemon development  
- Allow kernel modules and C++ daemons to be tested in a safe QEMU sandbox  
- Simulate networking behavior for routing, forwarding, and telemetry collection  
- Offer a consistent, modular platform for learning Yocto, QEMU, and system-level C++  
- Document and automate every layer of the platform for reproducibility  

This project serves both as an engineering learning platform and a foundation
for building more advanced virtual edge devices.

---

## Non-Goals

The initial release of EdgeNetSwitch does **not** aim to:

- Emulate real industrial protocols (PROFINET, EtherCAT, Modbus, etc.)  
- Provide cycle-accurate GPIO timing  
- Perform high-throughput packet forwarding benchmarks  
- Implement a full production-grade network switch firmware  
- Support CPU architectures beyond ARM64 (for now)  

These may be considered for future versions.

---

## Layered Architecture

EdgeNetSwitch is structured into four major layers.
This layered design ensures clean separation of concerns and modular extensibility.

---

### 1. Virtual Hardware & OS Layer (QEMU + Yocto)

- ARM64 virtual board (qemu-system-aarch64)
- Custom Yocto image with meta-edgenetswitch layer
- Bootloader → Kernel → Init → Systemd pipeline
- Platform device definitions via device tree
- Root filesystem structure and daemon deployment

This layer provides the fundamental operating system and hardware abstraction.

---

### 2. Kernel Layer

- Out-of-tree kernel modules
- Pseudo-GPIO / dummy sensor drivers
- Sysfs and procfs interfaces
- Netlink or ioctl mechanisms for userspace interaction
- Kernel log tracing and debugging tools

This layer enables driver development and hardware simulation.

---

### 3. User-Space Daemon Layer (C++20)

The central logic of EdgeNetSwitch:

- Logging subsystem (thread-safe)
- Configuration loader (JSON)
- MessagingBus (pub/sub event system)
- Telemetry engine (metrics, counters, health)
- Routing & switching engine (packet simulation)
- Packet lifecycle accounting with runtime-owned `lifecycle_id`
- Deterministic failure injection and replay validation
- System health monitoring
- Graceful shutdown & signal handling

This daemon represents the “firmware logic” of the edge device.

---

### 4. Runtime Replay & Determinism Layer

The daemon includes a deterministic replay subsystem for validating that the
same ingress stream produces the same observable terminal history.

- `ReplayRecorder` subscribes to `PacketRx` and writes ordered `ReplayRecord`
  entries.
- `ReplayPlayer` republishes those records as `PacketRx`, preserving the
  original ingress stream.
- `ReplayOutcomeCollector` subscribes to `PacketProcessed` and `PacketDropped`
  and records ordered terminal outcomes.
- `FailureInjector` supports lifecycle-keyed rules so injected failures can be
  reproduced deterministically during replay.

Replay records are intentionally ingress-only. They do not store processed or
dropped outcomes. During replay, the runtime must regenerate terminal events
from the recorded ingress stream and the active deterministic policy.

---

### 5. Tooling & Testing

- CLI tool: `edgenetctl`
- QEMU automation scripts
- Daemon deployment scripts
- Integration tests
- Replay outcome equivalence tests
- Deterministic failure-replay tests
- Developer documentation (Markdown)
- DevLogs tracking engineering progress

This layer improves usability, testing, and developer experience.

---

## High-Level Data Flow

           +----------------------+
           |      QEMU (ARM64)    |
           |  - Kernel + Yocto    |
           |  - Device Tree       |
           +----------+-----------+
                      |
                      | syscalls / IO / netlink
                      v
            +----------------------+
            |   Kernel Modules     |
            |  (dummy gpio, etc.)  |
            +----------+-----------+
                      |
                      | ioctl / netlink
                      v
            +----------------------+
            |   User-Space Daemon  |
            |  - Logger            |
            |  - MessagingBus      |
            |  - Routing Engine    |
            |  - Telemetry Engine  |
            |  - Replay Validation |
            +----------+-----------+
                      |
                      | CLI / JSON config
                      v
            +----------------------+
            |     Tooling Layer    |
            |  edgenetctl, scripts |
            +----------------------+

---

## Replay Determinism Model

Replay validation is based on observable terminal equivalence, not on internal
thread timing or incidental scheduling behavior.

Identity is split deliberately:

- `packet.id` is payload identity. It comes from traffic and may be reused or
  controlled by the sender.
- `lifecycle_id` is runtime-owned execution identity. It identifies one packet
  lifecycle from `PacketRx` to exactly one terminal event.

The replay path is:

1. `ReplayRecorder` observes `PacketRx` and records `(sequence, packet)`.
2. `ReplayPlayer` publishes the recorded packets back onto `MessagingBus`.
3. The normal runtime path applies admission, validation, failure injection, and
   processing.
4. `ReplayOutcomeCollector` records terminal observable events as ordered
   `ReplayOutcome` values.

Replay equivalence compares terminal history:

- terminal ordering
- lifecycle ordering
- outcome type (`Processed` or `Dropped`)
- drop attribution for dropped lifecycles

This model keeps replay validation production-relevant. The replay stream
contains only ingress, while processed and dropped outcomes must be reproduced
by the runtime itself.

## Deterministic Failure Replay

Failure replay uses deterministic runtime policy rather than storing failures in
the replay stream.

Count-based rules remain useful for workload-level fault scheduling, but
lifecycle-keyed rules provide stable replay of specific failures:

- A rule targets a `lifecycle_id`.
- The configured failure type is applied when that lifecycle is observed.
- Terminal failures publish `PacketDropped` with causal attribution.
- Non-terminal failures alter execution conditions and later converge through
  the normal terminal path.

Because failure replay is keyed by `lifecycle_id`, replay behavior does not
depend on externally supplied `packet.id` values. This preserves deterministic
failure reproduction even when payload IDs are duplicated, malformed, or sender
controlled.

The resulting guarantee is narrow and explicit: given the same ingress stream
and deterministic failure policy, replayed execution must produce the same
observable terminal history.

---

## Development Workflow

Each module follows a consistent development cycle:

1. **Design** — architectural reasoning & interfaces  
2. **Explanation** — rationale & alternatives  
3. **Minimal Example** — prototype code or pseudo-code  
4. **Production Implementation** — high-quality, documented code  
5. **Commit Message** — conventional commits  
6. **Documentation Snippet** — added to docs/ or devlog/  

This workflow mirrors the processes used in real embedded Linux teams.

---

## Summary

EdgeNetSwitch provides a complete end-to-end simulation environment for embedded
Linux systems. By combining QEMU-based virtual hardware, a Yocto-built operating
system, custom kernel modules, and a C++20 daemon with networking simulation,
the platform enables deep understanding of real-world device development —
without requiring any physical hardware.
