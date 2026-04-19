# Threading Model

## Overview
EdgeNetSwitch uses a bounded set of long-lived OS threads with explicit ownership of I/O and runtime orchestration. The concurrency model is a mix of synchronous event dispatch and asynchronous processing: `MessagingBus` dispatches synchronously on the calling thread, while packet processing crosses an explicit queue boundary into a worker thread. A logical stage (parse, admit, process, terminal publish) is not automatically a separate thread; thread separation exists only where the implementation inserts a queue or dedicated I/O loop. The main loop remains deterministic and tick-driven for telemetry, health evaluation, and snapshot publication.

## Threads in the System

### Main thread
- Initializes components and registers subscribers
- Runs deterministic tick loop (`telemetry`, `healthMonitor`, status build/publish)
- Coordinates lifecycle and shutdown

### PacketProcessor worker thread
- Drains bounded queue
- Performs validation and terminalization
- Publishes `PacketProcessed` / `PacketDropped`

### Control socket thread
- Handles IPC (`accept → read → dispatch → write`)
- Blocking I/O loop

### UDP receiver thread (if enabled)
- Receives datagrams
- Performs parse-level validation
- Publishes `PacketRx` for accepted packets
- May publish `PacketDropped` for parse-level rejection

## MessagingBus Interaction
- `publish()` is synchronous (no scheduling or deferral)
- Callbacks execute on the publisher thread (thread-affinity)
- Dispatch is sequential per call
- Caller is blocked until all callbacks complete
- Callback execution order follows subscription order for a given MessageType

## Asynchronous Boundaries
The primary asynchronous processing boundary in the packet path is the PacketProcessor queue handoff. Packet ingress may originate from multiple threads (e.g., UDP receiver), but processing is deferred to a single worker thread. This isolates processing latency and enforces bounded admission.

## Execution Flow Example
1. UDP thread receives packet
2. Calls `publish(PacketRx)` (synchronous, no deferral, execution completes before returning)
3. PacketProcessor admission runs on UDP thread
4. Packet is enqueued OR dropped immediately
5. Worker thread processes packet
6. Worker publishes terminal event
7. Subscribers execute on worker thread

## Key Characteristics
- Deterministic main loop
- Explicit thread-affinity
- Synchronous event dispatch
- Single async processing boundary
- No implicit parallelism

## Notes
- Slow subscribers extend the critical path of the publishing thread
- Latency propagates across the system (UDP ingress, main loop)
- Backpressure is explicit via queue overflow
- System is predictable but execution cost is coupled to publisher thread
- Design favors determinism and observability over parallel throughput

## Event Flow Diagram

1. **UDP ingress (UDP thread)**: Datagram arrives; parser/ingress validation runs. Invalid input emits `PacketDropped` (parse/ingress rejection) via `MessagingBus::publish()`.
2. **Ingress publish (UDP thread, synchronous)**: For accepted input, UDP thread publishes `PacketRx`. `publish()` runs matching callbacks immediately on the UDP thread and returns only after completion.
3. **Admission callback (UDP thread)**: PacketProcessor `PacketRx` subscriber executes on the UDP thread (thread-affinity). It performs bounded admission: enqueue packet or emit immediate `PacketDropped(reason=QueueOverflow)`.
4. **Async handoff boundary**: Enqueue operation transfers work from UDP thread to PacketProcessor queue; this is the primary asynchronous processing boundary.
5. **Processing stage (PacketProcessor worker thread)**: Worker dequeues packet and performs processor-stage validation/finalization.
6. **Terminal publish (worker thread, synchronous)**: Worker publishes terminal event: `PacketRx → PacketProcessed` on success, or `PacketRx → PacketDropped` on processor-stage failure. Dispatch is synchronous and sequential on the worker thread.
7. **Terminal subscribers (worker thread)**: Subscribers for `PacketProcessed`/`PacketDropped` execute on the worker thread for that publish call (no implicit parallelism inside bus dispatch).
8. **Concurrent runtime loop (main thread)**: Main thread independently publishes periodic telemetry/health events; those callbacks execute synchronously on the main thread and are not deferred by `MessagingBus`.
