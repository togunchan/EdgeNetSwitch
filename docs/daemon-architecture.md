# EdgeNetSwitch — Daemon & MessagingBus Architecture

The EdgeNetSwitch daemon is the central user-space component of the platform.
It represents the runtime firmware logic of a virtual industrial edge device
and is designed to closely mirror real-world embedded Linux systems.

The daemon runs as a long-living process (systemd-managed in later stages) and
coordinates all internal subsystems through a strict, event-driven architecture
built around the **MessagingBus**.

This design prioritizes modularity, testability, and long-term maintainability.

---

## Daemon Goals

The primary goals of the EdgeNetSwitch daemon are:

- Act as the central control plane of the virtual edge device  
- Orchestrate all runtime behavior without tight coupling between modules  
- Provide a clean separation between kernel space and user space  
- Enable deterministic, observable, and testable system behavior  
- Reflect real industrial firmware architectures used in production devices  

The daemon is intentionally designed as a **system-level component**, not a
simple application.

---

## Non-Goals

The daemon architecture explicitly does **not** aim to:

- Implement real industrial fieldbus protocols (e.g., PROFINET, EtherCAT)  
- Provide hard real-time guarantees  
- Perform high-throughput data-plane packet forwarding  
- Serve as a general-purpose Linux service framework  
- Replace production-grade switch/router firmware  

These constraints keep the scope focused and realistic for a virtual platform.

---

## Position in the Overall Architecture

The daemon resides entirely in the **User-Space Daemon Layer** and interacts with
other layers as follows:

- Receives events and data from kernel modules (via netlink, ioctl, or sysfs)
- Processes internal logic using modular subsystems
- Exposes control and inspection interfaces to tooling (CLI, telemetry)

The daemon never directly manipulates hardware and never bypasses the kernel.

---

## High-Level Daemon Structure

The daemon is composed of independent subsystems:

- Logger  
- MessagingBus  
- Configuration Manager  
- Routing Engine  
- Telemetry Engine  
- Health Monitor  
- CLI / Control Interface  

Each subsystem:

- Owns its internal state  
- Avoids direct dependencies on other subsystems  
- Communicates exclusively through MessagingBus  

This structure enforces loose coupling and predictable system behavior.

---

## Why MessagingBus Is Mandatory

Direct function calls between subsystems introduce:

- Tight coupling  
- Hidden execution paths  
- Difficult test isolation  
- Fragile long-term evolution  

MessagingBus eliminates these issues by enforcing **event-based communication**.

Subsystems publish messages without knowing who consumes them.
Subscribers react to messages without knowing who produced them.

This pattern is standard in:

- Industrial controllers  
- Network appliances  
- Automotive ECUs  
- Telecom infrastructure  

---

## MessagingBus Responsibilities

MessagingBus is responsible for:

- Message publication (`publish`)  
- Subscriber registration (`subscribe`)  
- Thread-safe message dispatch  
- Fan-out delivery to multiple subscribers  
- Basic routing based on message type  

MessagingBus is **not** responsible for:

- Network transport  
- Serialization or encoding  
- Persistent storage  
- Kernel communication logic  
- Scheduling or prioritization  

Those concerns are handled by dedicated adapters or subsystems.

---

## Message Model

All internal communication is expressed as immutable messages.

Each message contains:

- A `MessageType` identifier  
- A timestamp  
- An optional structured payload  

Once published, a message is never modified.

This immutability simplifies reasoning, testing, and concurrency.

---

## Publish / Subscribe Model

### Publishers

- Any subsystem may publish messages  
- Publishing is non-blocking  
- Publishers are unaware of subscribers  

### Subscribers

- Subsystems register callback functions  
- Multiple subscribers per message type are allowed  
- Callbacks execute in a controlled context  

A single message may trigger:

- Logging  
- Telemetry updates  
- Routing decisions  
- Health checks  

simultaneously.

---

## Threading and Concurrency Model

MessagingBus guarantees:

- Thread-safe `publish` and `subscribe` operations  
- Internal synchronization using standard C++ primitives  
- Best-effort ordering per message type  

Initial implementation uses:

- `std::mutex`  
- `std::lock_guard`  

Future versions may introduce:

- Worker threads  
- Message queues  
- Lock-free data structures  
- Priority-aware dispatching  

The current design favors clarity and correctness over premature optimization.

---

## Lifecycle Integration

MessagingBus lifecycle is tied to the daemon lifecycle:

1. Constructed during daemon startup  
2. Subsystems register subscriptions  
3. Runtime message flow begins  
4. Graceful shutdown drains and terminates cleanly  

MessagingBus contains no global state and can be instantiated per daemon instance.

---

## Testability

MessagingBus is designed to be:

- Unit-testable in complete isolation  
- Independent of OS-specific mechanisms  
- Runnable without QEMU or Yocto  

This allows fast feedback cycles and reliable CI integration.

---

## Future Extensions

MessagingBus serves as a stable core for future extensions:

- Kernel ↔ userspace adapters (netlink-based)  
- CLI command routing  
- Telemetry streaming  
- Fault injection mechanisms  
- Network message injection  

The core bus remains unchanged while adapters evolve around it.

---

## Summary

The EdgeNetSwitch daemon uses MessagingBus as its internal backbone to enforce a
clean, event-driven architecture.

This approach:

- Mirrors real industrial embedded Linux devices  
- Enables modular growth without architectural erosion  
- Provides a solid foundation for kernel integration and networking simulation  

The result is a realistic, maintainable, and extensible system-level platform.