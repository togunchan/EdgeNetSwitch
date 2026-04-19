# Packet Lifecycle

## Overview

Each packet entering the system follows a strictly defined lifecycle:

`PacketRx → PacketProcessed | PacketDropped`

Each packet is expected to reach exactly one terminal outcome under normal operation.

---

## Lifecycle Stages

### 1. Admission (PacketProcessor — PacketRx subscriber)

When a `PacketRx` message is received:

- If the internal queue is full (`MAX_QUEUE_SIZE = 1024`):
  - The packet is immediately dropped
  - `PacketDropped(reason = QueueOverflow)` is published

- Otherwise:
  - The packet is accepted
  - It is enqueued for asynchronous processing

---

### 2. Processing (Worker Thread)

Packets in the queue are processed asynchronously:

- If the packet is invalid:
  - `payload.size() > MAX_PAYLOAD_SIZE`
  - or `timestamp_ms == 0`
  
  → `PacketDropped(reason = ValidationError)` is published

- Otherwise:
  - The packet is considered valid
  - `PacketProcessed` is published

---

## Terminal Events

A packet reaches a terminal state when one of the following is emitted:

- `PacketProcessed`
- `PacketDropped`

Each terminal event represents the final outcome of a packet.

### Accounting

- `terminal_events` counts all terminal messages
- `duplicate_events` counts repeated terminal events for the same `packet_id`

---

## Pending Terminal Events

`pending_terminal_events` represents packets that have been received but not yet completed their lifecycle.

Defined as:

`pending_terminal_events = max(ingress_packets - terminal_events, 0)`

### Interpretation

- `> 0` → some packets are still being processed
- `= 0` → all observed packets have reached a terminal state

---

## Invariants

The following relationships must always hold:

- `terminal_events = processed_packets + sum(drops_by_reason)`
- `pending_terminal_events = max(ingress_packets - terminal_events, 0)`

### Lifecycle Consistency

For a correct system:

- `ingress_packets = terminal_events + pending_terminal_events`

At steady state (no in-flight packets):

- `pending_terminal_events = 0`
- `ingress_packets = terminal_events`