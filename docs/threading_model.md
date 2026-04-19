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
