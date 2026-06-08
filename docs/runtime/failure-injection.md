# Failure Injection

## Purpose

Deterministic failure injection gives EdgeNetSwitch a controlled way to exercise packet lifecycle error paths under production-like runtime conditions. It validates that parsing, validation, admission, processing, and telemetry preserve the same accounting contracts when traffic is dropped, delayed, or rejected intentionally.

The feature is not a chaos mechanism. It is an architectural test boundary for proving that observable runtime state remains consistent under known fault schedules.

## Deterministic Scheduling

Failures are scheduled by packet count through `every_n_packets`. A configuration of `5` affects every fifth observed `PacketRx` handled by the injector. A value of `0`, disabled injection, or `FailureType::None` produces no injection.

Count-based scheduling is preferred over randomness because it makes failure positions reproducible across runs, tests, and telemetry assertions. Reproducibility matters more than statistical variety for lifecycle accounting: the system must prove that specific ingress lifecycles produce specific terminal outcomes and that no packet disappears between `PacketRx` and accounting.

Random loss can be useful for workload simulation, but it is a weaker correctness tool. It obscures causality, complicates regression diagnosis, and turns invariant failures into seed-dependent behavior.

For replay validation, `FailureConfig::lifecycle_rules` can target specific
`lifecycle_id` values. Lifecycle-keyed rules are evaluated before count-based
rules, which lets replay tests reproduce the same injected terminal failures
without depending on payload `packet.id` or incidental scheduling outside the
recorded ingress stream.

## Lifecycle-Aware Terminal Accounting

Accounting is based on `lifecycle_id`, not payload `packet.id`.

`lifecycle_id` identifies one runtime packet lifecycle from ingress to terminal outcome. `packet.id` is payload identity and may be duplicated, malformed, or otherwise unsuitable for correctness accounting.

`PacketStats` subscribes to `PacketRx`, `PacketProcessed`, and `PacketDropped`:

- `PacketRx` increments ingress.
- `PacketProcessed` increments processed counters and records a terminal lifecycle.
- `PacketDropped` increments the drop counter for its reason and records a terminal lifecycle.

For each `lifecycle_id`, exactly one terminal event is valid. Repeated terminal events for the same lifecycle increment `duplicate_events` and are treated as lifecycle contract violations.

Core invariants:

- `terminal_events = processed_packets + sum(drops_by_reason)`
- `pending_terminal_events = max(ingress_packets - terminal_events, 0)`
- At steady state, `ingress_packets = terminal_events`
- `duplicate_events = 0` for a correct pipeline

These invariants make failure injection useful as a correctness probe rather than a counter-only metric source.

## Terminal and Non-Terminal Failures

Injected failures are classified by whether they complete the lifecycle immediately.

Terminal failures publish `PacketDropped` and stop further processing for that lifecycle. The drop is the final outcome and must carry the original `lifecycle_id`.

Non-terminal failures alter timing or execution conditions without creating a terminal event. The packet remains active and must later produce either `PacketProcessed` or `PacketDropped` through the normal pipeline.

Current terminal failure types:

- `MalformedPacket`
- `ValidationError`
- `SimulatedLoss`
- `ProcessingRejection`

Current non-terminal failure type:

- `ArtificialDelay`

## ArtificialDelay Semantics

`ArtificialDelay` delays the `PacketRx` subscriber path before queue admission. It does not publish `PacketDropped`, does not increment drop counters, and does not complete the lifecycle.

After the delay, the packet follows normal admission:

- It is enqueued if the processor queue has capacity.
- It is dropped with `QueueOverflow` if the queue is full.
- If admitted, it later completes as `PacketProcessed` or processor-stage `PacketDropped`.

This makes `ArtificialDelay` a timing perturbation, not a loss model. It is useful for exposing pressure, callback latency, scheduling sensitivity, and backlog behavior without changing terminal accounting semantics.

## Overload Interaction

Failure injection runs before processor queue admission. Terminal injected failures bypass the queue and publish a final `PacketDropped` event immediately. Non-terminal injection continues to admission and can still encounter overload.

Queue overload remains a separate system failure mode represented by `PacketDropped(reason = QueueOverflow)`. It is not a `FailureType`; it is produced by the bounded admission policy when the internal processor queue reaches `MAX_QUEUE_SIZE`.

Under pressure, injected terminal failures and overload drops may coexist. Their reasons must remain distinct:

- `SimulatedLoss` means the injector intentionally terminated the lifecycle.
- `QueueOverflow` means admission rejected the lifecycle because capacity was exhausted.

Keeping these causes separate preserves operator diagnosis. A single aggregate drop count would hide whether loss came from configured fault injection, malformed input, policy rejection, processing failure, or actual overload.

## Supported Failure Types

| FailureType | Terminal | PacketDropped reason | Semantics |
| --- | --- | --- | --- |
| `None` | No | None | Injection disabled by type. |
| `MalformedPacket` | Yes | `ParseError` | Models a parser-stage terminal failure. |
| `ValidationError` | Yes | `ValidationError` | Models validation rejection. |
| `SimulatedLoss` | Yes | `SimulatedLoss` | Models intentional packet loss. |
| `ArtificialDelay` | No | None | Sleeps for `delay_ms`, then resumes normal admission. |
| `ProcessingRejection` | Yes | `ProcessingError` | Models processor-stage rejection. |

Unsupported or unexpected terminal mappings should not be normalized silently. If a new failure type is added, it must declare whether it is terminal and how it maps to observable packet events.

## Testing Philosophy

Failure-injection tests assert contracts, not incidental timing.

The important behavior is deterministic lifecycle convergence:

- The configured schedule triggers exactly on the expected packet positions.
- Every ingress lifecycle reaches exactly one terminal event unless intentionally still pending.
- Terminal injected failures preserve `lifecycle_id`.
- `ArtificialDelay` creates no terminal event by itself.
- `PacketStats` remains internally consistent under injected loss and queue pressure.
- Drop reasons identify cause without collapsing unrelated failure modes.
- Replay with the same ingress stream and lifecycle-keyed failure policy
  produces the same ordered terminal outcomes.

Because packet processing is asynchronous, tests should use bounded eventual assertions for terminal convergence. Immediate synchronous assertions are valid only for behavior that is explicitly synchronous, such as deterministic injection order on the current `MessagingBus` dispatch path.

Replay equivalence is validated through observable terminal history: outcome
order, lifecycle order, terminal type, and drop reason must match between the
original execution and replayed execution. The replay stream stores ingress
only; failures are reproduced by deterministic runtime policy.

## PacketStats and PacketDropped

`PacketDropped` is the runtime contract between failure sources and accounting. Injected terminal failures, parser/validator rejection, queue overflow, and processor rejection all converge through this event type with a reason and `lifecycle_id`.

`PacketStats` does not need to know whether a drop was natural or injected. It only requires that every terminal drop is published once with the correct lifecycle identity and causal reason.

This separation keeps responsibilities narrow:

- Failure injection chooses when and how to perturb a lifecycle.
- Packet processing enforces admission and processing behavior.
- `PacketDropped` carries terminal loss attribution.
- `PacketStats` enforces lifecycle accounting invariants.

The result is production-oriented observability: injected failures exercise the same event path and counters that real failures use, without adding a parallel accounting model.
