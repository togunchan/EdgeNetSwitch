# Switching Runtime

This document describes the current v1.9 switching subsystem behavior. It covers
the runtime model implemented by `SwitchForwardingEngine`, `MacTable`,
`InterfaceRegistry`, `SwitchPort`, and `ForwardingDecision`.

The switching subsystem is currently an in-process decision layer integrated
with the packet processor runtime path. It computes a forwarding decision from a
parsed packet, an ingress port, the current MAC table, and the registered port
states. It does not transmit packets itself.

## Responsibilities

The subsystem owns four narrow responsibilities:

- Maintain learned MAC-to-port mappings in `MacTable`.
- Maintain port metadata and operational state in `InterfaceRegistry`.
- Convert packet MAC fields into a `ForwardingDecision`.
- Keep forwarding results deterministic for unit tests and replay-oriented
  runtime reasoning.

`SwitchForwardingEngine::processPacket()` is the decision boundary. The caller
supplies the packet, ingress port, and logical tick. The engine returns one of:

- `Drop`
- `Flood`
- `ForwardToPorts`

The returned `egress_ports` vector is the full output of the forwarding
decision.

## Runtime Integration

`PacketProcessor` optionally owns access to a `SwitchForwardingEngine`. After
worker-side validation and before publishing `PacketProcessed`, it invokes the
engine when the packet contains an `ingress_port`.

The processor publishes:

```text
ForwardingDecisionMade -> PacketProcessed
```

for successfully processed packets that have switching context. Packets without
an ingress port still complete through the normal packet lifecycle, but no
forwarding event is emitted. Validation failures and injected terminal failures
remain drop outcomes and do not produce forwarding decisions.

`ForwardingDecisionMade` carries:

```text
lifecycle_id
action
egress_ports
```

This makes forwarding observable without making the forwarding engine a packet
transmitter or coupling observers to processor internals.

## Control-plane Interaction

The control plane can publish synthetic packet ingress and inspect learned
switching state through the normal dispatch path:

```text
CLI -> UNIX socket -> dispatchControlRequest -> dispatchV12 -> handler
```

`send-packet:broadcast` injects a deterministic broadcast packet as `PacketRx`.
`send-packet:learn` injects a deterministic learning sequence. Both commands use
`MessagingBus` and the normal packet processor path, so MAC learning and
forwarding decisions occur in the same runtime path as other packet ingress.

`show:mac-table` reads the forwarding engine's MAC table snapshot and returns
deterministic text output. It is an inspection command; it does not mutate the
table or age entries.

## Forwarding Semantics

Packets without both MAC fields produce a `Drop` forwarding decision immediately.
No MAC learning occurs for these packets.

For packets with both MAC addresses, the source MAC is learned on the ingress
port before the destination decision is made.

Broadcast destinations are flooded to all currently `Up` ports except the
ingress port.

Unknown unicast destinations are also flooded to all currently `Up` ports except
the ingress port.

Known unicast destinations are forwarded only to the learned destination port if
that port is currently `Up`. If the learned port is `Down` or unknown to the
registry, the packet is dropped. The engine does not fall back to flooding in
that case.

Example:

```text
Ports: 1 Up, 2 Up, 3 Up, 4 Down
Ingress: 2
Destination: ff:ff:ff:ff:ff:ff
Decision: Flood to [1, 3]
```

Example:

```text
MAC table: 00:11:22:33:44:02 -> port 4
Port 4: Down
Ingress: 2
Destination: 00:11:22:33:44:02
Decision: Drop
```

Real Ethernet switches commonly suppress same-port forwarding for known-unicast traffic, but the current implementation intentionally leaves this behavior explicit and observable. If a destination MAC is learned on the same port as the incoming packet,
the engine returns `ForwardToPorts` with that same port.

## MAC Learning

`MacTable` stores entries as:

```text
MAC address -> { port_id, last_seen_tick }
```

Learning behavior:

- A packet source MAC is learned on every valid switching decision path before
  destination lookup.
- Relearning an existing MAC updates both `port_id` and `last_seen_tick`.
- A MAC can move between ports; the latest learned ingress port wins.
- A table with capacity `0` ignores all learning.
- The table does not validate whether the learned ingress port exists or is
  `Up`.

Example:

```text
Tick 10: source 00:11:22:33:44:01 arrives on port 1
MAC table: 00:11:22:33:44:01 -> port 1

Tick 11: same source arrives on port 3
MAC table: 00:11:22:33:44:01 -> port 3
```

## InterfaceRegistry

`InterfaceRegistry` is the subsystem's source of port identity and operational
state. It stores `SwitchPort` objects by numeric port ID.

It provides:

- `findPort(port_id)` for port lookup.
- `isUp(port_id)` for operational state checks.
- `setState(port_id, state)` for state changes.
- `activePortIds()` for flood candidate selection.
- `snapshot()` for deterministic inspection.

Unknown ports are treated as not up. Setting the state of an unknown port is a
no-op.

Ports are stored in a `std::map`, so `activePortIds()` and `snapshot()` return
ports in ascending numeric port ID order. Adding a port with an existing ID uses
`emplace`, so it does not replace the existing registered port.

## Deterministic Behavior Guarantees

The switching subsystem uses ordered maps for both MAC entries and registered
ports. This gives stable iteration order and stable output vectors for the same
input state.

Deterministic properties:

- Flood egress ports are ordered by ascending port ID.
- Interface snapshots are ordered by ascending port ID.
- MAC table snapshots are ordered by MAC address.
- Capacity eviction scans entries in MAC-address order and removes the oldest
  `last_seen_tick`; ties remove the lowest MAC address among tied entries.
- Packet decisions depend only on packet MAC fields, ingress port, supplied
  tick, MAC table state, and interface registry state.

The engine does not use wall-clock time, random selection, background work, or
external I/O.

## Aging and Eviction

MAC aging is explicit. `SwitchForwardingEngine::processPacket()` does not call
`MacTable::ageOut()`. A caller must invoke aging separately.

`ageOut(current_tick, max_age)` removes entries when:

```text
current_tick - last_seen_tick > max_age
```

The comparison is strictly greater than `max_age`. An entry exactly at the age
limit is retained.

Example:

```text
Entry last seen at tick 80
Current tick 100
max_age 20
Age: 20
Result: retained

Entry last seen at tick 79
Current tick 100
max_age 20
Age: 21
Result: removed
```

Capacity eviction happens during learning when the table is full and the learned
MAC is new. The entry with the smallest `last_seen_tick` is removed. If multiple
entries have the same oldest tick, the lowest MAC address is removed because the
table is ordered by MAC address and the scan keeps the first tied candidate.

## Operational Port State

Port state is binary:

- `Down`
- `Up`

Flooding uses only ports currently marked `Up`, and always excludes the ingress
port from the flood list.

Known-unicast forwarding checks only the learned destination port state. If that
port is not `Up`, the decision is `Drop`.

Ingress state is not checked. A packet arriving on a `Down` or unregistered
ingress port can still cause source MAC learning and can still produce a flood
or forwarding decision based on the rest of the registry state.

## Current Limitations

- The switching engine is a decision component; it is wired into the daemon
  packet-processing path for learning and decision publication, but not as a
  packet transmitter.
- There is no VLAN, STP, link aggregation, multicast group, ACL, QoS, or routing
  behavior.
- Known-unicast traffic learned on the ingress port is forwarded back to that
  same port instead of being suppressed.
- Ingress port state is not validated before learning or forwarding.
- MAC aging is not automatic; callers must invoke `ageOut()` explicitly.
- Learned entries are not removed automatically when a port transitions to
  `Down`.
- Broadcast source MACs, zero MACs, and other unusual source addresses are not
  rejected by the switching engine if the packet carries both MAC fields.
- `InterfaceRegistry` is not internally synchronized; concurrent access requires
  external ownership discipline.
- `MacTable` is not internally synchronized; concurrent learning, lookup,
  aging, or snapshot access requires external ownership discipline.
