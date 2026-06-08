# v1.8.9 Replay Determinism

## Replay Architecture

v1.8.9 completes replay-verifiable deterministic runtime behavior.

Replay is modeled as an ingress-only stream:

- `ReplayRecorder` captures ordered `PacketRx` records.
- `ReplayRecord` stores replay sequence and packet ingress state.
- `ReplayPlayer` republishes recorded ingress through `MessagingBus`.
- Runtime processing regenerates terminal outcomes.
- `ReplayOutcomeCollector` captures ordered terminal observable history.

The replay stream does not store processed or dropped outcomes. Replay is valid
only when the runtime reproduces the same terminal observable history from the
same ingress stream and deterministic policy.

## Identity Model

- `packet.id` is payload identity.
- `lifecycle_id` is runtime-owned execution identity.

Correctness accounting, deterministic failure replay, and replay outcome
validation use `lifecycle_id`. Payload packet IDs may be duplicated or supplied
by external traffic and are not suitable as runtime correctness keys.

## Equivalence Contract

Replay equivalence compares terminal observable history:

| Field | Requirement |
| --- | --- |
| `sequence` | Terminal events appear in the same order. |
| `type` | Processed and dropped outcomes match. |
| `lifecycle_id` | Runtime lifecycle ordering is preserved. |
| `drop_reason` | Dropped lifecycles preserve causal attribution. |

Aggregate packet metrics remain useful, but v1.8.9 validates replay at the
observable terminal-event boundary.

## Deterministic Failure Replay

Failure replay is reproduced through runtime policy, not serialized outcomes.

Lifecycle-keyed failure rules target specific `lifecycle_id` values and are
applied during normal processing. This lets replay reproduce injected loss while
keeping the replay stream limited to ingress.

The resulting guarantee is:

Given the same ingress records and deterministic failure policy, replayed
execution must produce the same ordered terminal outcomes as the original run.
