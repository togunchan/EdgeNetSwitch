# Threading Model

## Overview
EdgeNetSwitch runs with a bounded set of long-lived OS threads that split I/O and runtime responsibilities. The concurrency model is hybrid: `MessagingBus` dispatches synchronously on the calling thread, while asynchronous execution occurs only at explicit queue handoff points. A logical stage (parse, admit, process, terminal publish) does not automatically imply a separate thread; a separate thread exists only where the code explicitly starts a worker or an I/O loop. In the packet path, the primary asynchronous boundary is the `PacketProcessor` queue handoff; outside the packet path, telemetry export also uses a separate async queue/worker path.

## Threads in the System

### Main thread
- Initializes components, registers `MessagingBus` subscribers, and publishes lifecycle events.
- Runs the deterministic tick loop: `telemetry.onTick()`, `healthMonitor.onTick()`, status build/publish.
- Starts/stops subsystems and controls shutdown sequencing.

### PacketProcessor worker thread
- Waits on and drains the `PacketProcessor` bounded queue using `condition_variable`.
- Executes packet processing and terminalization.
- Publishes `PacketProcessed` or processor-stage `PacketDropped` events.

### Control socket thread
- Runs control IPC flow: `accept -> read -> dispatch -> write -> close`.
- Operates as a blocking socket I/O loop.

### UDP receiver thread (if enabled)
- Receives UDP datagrams (`recvfrom` loop).
- Performs UDP-stage parse/validation.
- Publishes `PacketRx` for accepted packets.
- Publishes `PacketDropped(ParseError|ValidationError)` for UDP-stage rejected packets.

### Telemetry export thread
- Waits on the `TelemetryExportManager` queue and sends samples to exporters.
- Asynchronously executes work handed off from the main-thread callback path via `enqueue()`.

## MessagingBus Interaction
- `MessagingBus::publish()` is synchronous; there is no scheduling or deferral.
- Callbacks run with thread affinity on the publisher thread.
- Fan-out inside a single `publish()` call is sequential; callbacks are invoked one by one.
- The publisher thread does not return from `publish()` until all matching callbacks complete.
- The bus has no internal worker thread, queue, or bus-level overload protection.
- There is no global dispatch serialization; concurrent `publish()` calls from different threads can execute callbacks concurrently.

## Asynchronous Boundaries
In the packet execution path, the primary asynchronous boundary is the `PacketProcessor` queue handoff: `PacketRx` admission runs synchronously on the publisher thread, while processing is deferred to the worker thread. This boundary separates processing latency from ingress and enforces bounded admission (`MAX_QUEUE_SIZE`). Outside the packet path, a second async boundary exists in telemetry export (`enqueue` -> export worker). Outside these boundaries, event propagation on `MessagingBus` remains synchronous.

## Execution Flow Example
1. The **UDP thread** receives a datagram and performs UDP-stage parse/validation.
2. If rejected at UDP stage, the **UDP thread** publishes `PacketDropped(ParseError|ValidationError)`; dispatch completes synchronously.
3. If accepted at UDP stage, the **UDP thread** calls `publish(PacketRx)` (synchronous, no deferral).
4. `PacketRx` subscribers execute on the **UDP thread**; the `PacketProcessor` admission callback either enqueues the packet or publishes `PacketDropped(QueueOverflow)`.
5. The enqueued packet crosses the async queue handoff and is picked up by the **PacketProcessor worker thread**.
6. The **worker thread** performs packet processing and terminalization.
7. The **worker thread** publishes a terminal event: `PacketRx -> PacketProcessed` (success) or `PacketRx -> PacketDropped` (processor-stage validation failure).
8. Terminal-event callbacks execute sequentially and synchronously on the same **worker thread**.
9. In parallel, the **main thread** publishes telemetry/health events; their callbacks run on the main thread, while telemetry export work is handed off asynchronously to the export worker.

## Key Characteristics
- Main runtime orchestration is tick-driven and deterministic.
- Event callback execution is thread-affine to the publisher thread.
- `MessagingBus` dispatch is synchronous, sequential, and scoped to each publish call.
- In the packet path, the primary async boundary is the `PacketProcessor` queue+worker pipeline.
- The additional async path is telemetry export; it does not convert packet processing into a parallel multi-worker model.
- There is no implicit parallel fan-out inside `publish()`.

## Notes
- A slow subscriber directly extends the publisher thread critical path (UDP ingress, main tick, or worker publish path).
- Because there is no bus-level queue, overload is not absorbed at the bus layer; it appears as increased publish latency.
- Packet backpressure is implemented in the `PacketProcessor` bounded queue, not in the bus; saturation is visible as `QueueOverflow` drops.
- The design prioritizes predictability and observability; throughput scaling is constrained by synchronous dispatch and single-worker processing in the packet path.

## Event Flow Diagram

1. **UDP ingress (UDP thread)**: A datagram is received and parse/UDP-stage validation runs.
2. **Early reject path (UDP thread, synchronous)**: Invalid input is published as `PacketDropped(ParseError|ValidationError)`.
3. **Ingress publish (UDP thread, synchronous)**: Valid input is published as `PacketRx`; the call does not return until UDP-thread callbacks finish.
4. **Admission (UDP thread)**: The `PacketProcessor` callback enqueues the packet or publishes `PacketDropped(QueueOverflow)`.
5. **Primary async handoff**: Enqueue transfers execution from the UDP thread to the PacketProcessor worker thread.
6. **Processing (worker thread)**: The worker dequeues and processes the packet.
7. **Terminal publish (worker thread, synchronous)**: `PacketRx -> PacketProcessed` or `PacketRx -> PacketDropped` is published.
8. **Terminal subscribers (worker thread)**: Matching callbacks run sequentially on the same worker thread.
9. **Main-loop events (main thread)**: Telemetry/health publish flow runs synchronously on the main thread; telemetry export is handed off asynchronously to the export thread.

## Known Limitations

### MessagingBus
- Dispatch is synchronous; there is no publish-time scheduling/deferral.
- Fan-out is sequential per publish call; there is no parallel callback fan-out.
- Subscriber cost is paid on the publisher thread.

### Thread-Affinity Consequences
- Callbacks run on the thread that calls `publish()`.
- UDP-thread publish paths can be delayed by slow subscribers on ingress.
- Main-thread publish paths can extend tick duration.

### Bus-Level Backpressure
- MessagingBus has no internal queue/buffer.
- There is no overload shedding or admission control at bus level.
- Under load, the system does not drop events at the bus level; instead, latency increases along the publisher thread execution path.

### PacketProcessor
- Packet processing is serialized through a single worker thread.
- The bounded queue can produce drops under burst traffic.
- Queue saturation is reported as `PacketDropped(reason=QueueOverflow)`.

### Latency Propagation
- A slow subscriber delays downstream callbacks in the same publish operation.
- Synchronous subscriber delay propagates into end-to-end path latency.
- Delay can spread across ingress, processing, and periodic runtime paths.

### Scalability Constraints
- The current design prioritizes determinism/observability over peak throughput.
- It is not optimized for high-core parallel scaling of callback workloads.
- Scaling is constrained by synchronous dispatch and single-worker processing in the packet path.
