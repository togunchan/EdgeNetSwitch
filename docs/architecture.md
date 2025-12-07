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
- System health monitoring
- Graceful shutdown & signal handling

This daemon represents the “firmware logic” of the edge device.

---

### 4. Tooling & Testing

- CLI tool: `edgenetctl`
- QEMU automation scripts
- Daemon deployment scripts
- Integration tests
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
            +----------+-----------+
                      |
                      | CLI / JSON config
                      v
            +----------------------+
            |     Tooling Layer    |
            |  edgenetctl, scripts |
            +----------------------+

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