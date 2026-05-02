# EdgeNetSwitch

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Version](https://img.shields.io/badge/version-v1.8.7-orange)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)

> Debugging embedded network systems after hardware integration is too late.  
> EdgeNetSwitch is a deterministic C++20 runtime for validating and reasoning about networked systems before hardware exists.

## Problem

Embedded networking systems are often debugged too late: after hardware is available, after kernel integration has started, and after concurrency bugs are already mixed with driver, BSP, and timing behavior.

That makes packet loss, shutdown races, observability gaps, and lifecycle accounting errors difficult to reproduce. The core runtime needs to be designed and validated before it is buried under platform-specific complexity.

## Solution

EdgeNetSwitch isolates the runtime as a deterministic execution environment with explicit control over concurrency, timing, and system behavior. It models packet ingress, processing, telemetry, health, control-plane inspection, overload behavior, and shutdown sequencing inside a controlled C++20 daemon.

The system enables early validation of:
- event flow through the runtime
- concurrency boundaries and ownership
- overload and backpressure behavior
- lifecycle correctness guarantees
- observability without timing side effects

## Key Engineering Highlights

- Deterministic runtime ownership: execution is driven by a tick loop, not external I/O.
- Determinism over throughput: the system prefers explicit loss to hidden latency or blocking.
- Explicit concurrency model: `MessagingBus` dispatch is synchronous and thread-affine; asynchronous behavior exists only at bounded queue handoffs.
- Lifecycle-based correctness: `lifecycle_id` is runtime identity, while `packet.id` remains payload identity.
- Auditable packet invariants: `terminal_events == processed_packets + total_drops` and `ingress_packets == terminal_events + pending_terminal_events`.
- Bounded async processing: packet admission has a fixed capacity, explicit `QueueOverflow` drops, backlog visibility, and drop attribution by reason.
- Observability-first design: telemetry export runs off the runtime path, and the read-only control plane returns structured JSON snapshots.
- Production-grade lifecycle management: RAII cleanup, coordinated shutdown, and thread ownership discipline.

## Architecture Overview

```mermaid
flowchart LR
    subgraph Data["Data Plane"]
        Traffic["Network Traffic"] --> Ingress["Packet Ingress"]
        Ingress --> Admission["Bounded Admission"]
    end

    subgraph Core["Deterministic Runtime Core"]
        Tick["Deterministic Tick Loop (Execution Owner)"] --> Bus["MessagingBus"]
        Admission --> Bus
        Bus --> Outcomes["Packet Outcomes"]
        Bus --> State["Runtime State"]
    end

    subgraph Control["Control Plane"]
        Operator["CLI / UNIX Socket"] --> Queries["Read-Only Queries"]
        Queries -. snapshots .-> State
    end

    subgraph Async["Explicit Async I/O Boundaries"]
        Admission --> WorkQueue["Packet Work Queue"]
        State --> ExportQueue["Telemetry Export Queue"]
        ExportQueue --> Exporters["File / Memory / Stdout"]
    end
```

The main tradeoff is intentional: the runtime prioritizes deterministic execution and explicit loss over hidden blocking, unbounded buffering, or timing side effects from observability paths.

The system enforces a strict boundary between deterministic execution and external I/O, ensuring predictable behavior under load.

## Tech Stack

- C++20, CMake
- Catch2 for unit coverage
- nlohmann/json for configuration and control responses
- POSIX UDP sockets and UNIX domain sockets
- `std::thread`, `std::mutex`, `std::condition_variable`, atomics
- macOS and Linux targets

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the architectural milestone history and release-level engineering notes.

## Intended Audience

This project is designed for engineers working on:
- embedded systems
- network runtimes
- event-driven architectures
- deterministic systems and observability

## Getting Started

### Clone

```bash
git clone https://github.com/togunchan/EdgeNetSwitch.git
cd EdgeNetSwitch
git submodule update --init --recursive
```

### Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

### Run

```bash
./build/EdgeNetSwitchDaemon
```

### Verify

```bash
ctest --test-dir build --output-on-failure
```

## Quick Demo (No Hardware Required)

Send a UDP packet to the runtime:

```bash
echo "test-packet" | nc -u 127.0.0.1 9000
```

Inspect system state:

```bash
echo "packet-stats:json" | nc -U /tmp/edgenetswitch.sock
```

This demonstrates deterministic packet ingestion, lifecycle tracking, and observable system state without hardware dependencies.

## Contributing

This project is primarily a systems architecture exploration.

Contributions, experiments, and technical discussions are welcome.

Open an issue for:
- architecture ideas
- runtime experiments
- documentation improvements

## Contact
[![LinkedIn - Murat Toğunçhan Düzgün](https://img.shields.io/badge/LinkedIn-Murat%20To%C4%9Fun%C3%A7han%20D%C3%BCzg%C3%BCn-blue.svg)](https://www.linkedin.com/in/togunchan/)
[![GitHub - togunchan](https://img.shields.io/badge/GitHub-togunchan-black.svg)](https://github.com/togunchan)
